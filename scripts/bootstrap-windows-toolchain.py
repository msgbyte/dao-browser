#!/usr/bin/env python3
"""Create a Chromium Windows SDK archive on macOS using Microsoft packages."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile


# The downloader resolves Microsoft's VS manifests and verifies payload SHA-256s.
# Only its download/extraction script is used; Wine and install.sh are not used.
DOWNLOADER_REVISION = '514f8ea34842cd6d831804d0e9658d3a32870ae1'
DOWNLOADER_SHA256 = '278429ffd7ec3aa0080ed0438fe241bb73795555949970cc569db68a60cc386b'
# Pin the bytes served by the fixed Microsoft HTTPS endpoint. Its channel's
# advertised size/hash differ from this response; do not derive this checksum
# from the URL or silently accept another response. Payload hashes remain those
# in the Microsoft manifest and are checked by vsdownload.py.
VS_MANIFEST_SHA256 = '7c3ba7bdc81a44b65b98dbc1c7cebc3d49249624a642f08ee7e8ae6fb7534867'
VS_MANIFEST_URL = ('https://download.visualstudio.microsoft.com/download/pr/'
                   'e5f740e0-92f9-49d7-ab3b-5d17b84108fd/'
                   '5a0474ac0d50b9805144617bc0e9fd93c18a7a80a1a82ec690e9bf6b8a25ccc8/VisualStudio.vsman')
SDK_SETUP_URL = ('https://download.microsoft.com/download/'
                 'f4b30f2a-4fc3-430e-9b03-c842b5f5f9f1/'
                 'KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/winsdksetup.exe')
SDK_SETUP_SHA256 = '6fa0fa27db77a909f5ecb35183cb26a969a6775936780936fe239e4f9c66b458'
SDK_VERSION = '10.0.26100.0'


def file_hash(file, algorithm='sha256'):
    digest = hashlib.new(algorithm)
    with file.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def download_verified(url, destination, expected_hash):
    algorithm = 'sha1' if len(expected_hash) == 40 else 'sha256'
    if destination.exists():
        if file_hash(destination, algorithm) != expected_hash.lower():
            raise ValueError(f'Cached file checksum mismatch: {destination}. Remove it and retry.')
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + '.partial')
    print(f'Downloading {destination.name}', flush=True)
    try:
        for attempt in range(3):
            try:
                with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as output:
                    shutil.copyfileobj(response, output)
                if file_hash(temporary, algorithm) != expected_hash.lower():
                    raise ValueError(f'Download checksum mismatch: {destination}')
                temporary.replace(destination)
                return
            except (OSError, ValueError):
                if attempt == 2:
                    raise
    finally:
        temporary.unlink(missing_ok=True)


def debugger_payloads(manifest):
    ns = {'b': 'http://schemas.microsoft.com/wix/2008/Burn'}
    payloads = {element.attrib['Id']: element.attrib for element in manifest.findall('b:Payload', ns)}
    package = manifest.find('.//b:MsiPackage[@Id="package_SDKDebuggers_x86_en_us"]', ns)
    if package is None:
        raise ValueError('Windows SDK manifest does not contain Debugging Tools for Windows.')
    result = []
    for ref in package.findall('b:PayloadRef', ns):
        payload = payloads[ref.attrib['Id']]
        relative = Path(payload['FilePath'].replace('\\', '/'))
        if relative.is_absolute() or '..' in relative.parts or relative.parts[0] != 'Installers':
            raise ValueError(f'Unsafe SDK payload path: {relative}')
        result.append((relative, payload['Hash']))
    return result


def install_debuggers(cache, destination):
    # VS's SDK component omits debuggers. The standalone SDK's signed bundle
    # contains the SHA-1s for its MSI/CAB payloads; pin the bundle with SHA-256.
    setup = cache / 'winsdksetup-10.0.26100.7705.exe'
    download_verified(SDK_SETUP_URL, setup, SDK_SETUP_SHA256)
    manifest = ET.fromstring(subprocess.check_output(['7zz', 'e', '-so', str(setup), '0']))
    payloads = debugger_payloads(manifest)
    directory = cache / 'debuggers'

    def download(payload):
        relative, digest = payload
        url = urllib.parse.urljoin(SDK_SETUP_URL, urllib.parse.quote(relative.as_posix()))
        download_verified(url, directory / relative, digest)

    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(download, payloads))
    # The parent MSI installs SDK Debuggers; the two nested MSIs are payloads.
    installer = directory / 'Installers/SDK Debuggers-x86_en-us.msi'
    with (cache / 'debuggers-extract.log').open('w') as log:
        subprocess.run(['msiextract', '-C', str(destination), str(installer)], check=True, stdout=log)
    extracted = destination / 'Program Files/Windows Kits/10/Debuggers'
    if extracted.exists():
        shutil.copytree(extracted, destination / 'Windows Kits/10/Debuggers', dirs_exist_ok=True)
        shutil.rmtree(destination / 'Program Files')


def package_toolchain(source, archives, vs_version, sdk_version):
    for file in source.rglob('*'):
        if file.is_symlink():
            raise ValueError(f'Unexpected symlink in Windows toolchain: {file}')
    versions = sorted((source / 'VC/Tools/MSVC').glob('14.*'),
                      key=lambda p: [int(n) for n in p.name.split('.')])
    if len(versions) != 1:
        raise ValueError('Expected exactly one MSVC toolset in the downloaded toolchain.')
    vc = versions[0].relative_to(source)
    sdk = Path('Windows Kits/10')
    includes = [vc / 'include', vc / 'atlmfc/include'] + [
        sdk / 'Include' / sdk_version / part for part in ['um', 'shared', 'winrt', 'ucrt']]
    required = [vc / 'include/vector', vc / 'atlmfc/include/atlbase.h',
                sdk / 'Include' / sdk_version / 'um/Windows.h', Path('DIA SDK/bin/amd64/msdia140.dll')]
    environments = {}
    runtime_copies = []
    # GN loads the ARM64 environment on Apple Silicon even for Windows x64.
    for arch, runtime in [('x86', 'sys32'), ('x64', 'sys64'), ('arm64', 'sysarm64')]:
        bins = [vc / 'bin/Hostx64' / arch, sdk / 'bin' / sdk_version / arch]
        libs = [vc / 'lib' / arch, vc / 'atlmfc/lib' / arch,
                sdk / 'Lib' / sdk_version / 'um' / arch,
                sdk / 'Lib' / sdk_version / 'ucrt' / arch]
        required += [bins[0] / 'cl.exe', libs[0] / 'libcmt.lib', libs[1] / 'atls.lib',
                     libs[2] / 'kernel32.lib', libs[3] / 'ucrt.lib',
                     bins[1] / 'ucrt/ucrtbased.dll']
        if arch != 'arm64':
            required.append(sdk / 'Debuggers' / arch / 'dbghelp.dll')
        redist = source / 'VC/Redist/MSVC'
        for suffix, pattern in [('', f'14.*/{arch}/Microsoft.VC*.CRT'),
                                ('d', f'14.*/debug_nonredist/{arch}/Microsoft.VC*.DebugCRT')]:
            directories = list(redist.glob(pattern))
            if len(directories) != 1:
                raise ValueError(f'Missing or ambiguous MSVC runtime: {pattern}')
            names = ['msvcp140', 'msvcp140_atomic_wait', 'vcruntime140', 'vccorlib140']
            for name in names + (['vcruntime140_1'] if arch == 'x64' else []):
                # atomic_wait is the exception; vcruntime140_1d ends in d.
                dll = (name.replace('140', '140' + suffix) if name == 'msvcp140_atomic_wait'
                       else name + suffix) + '.dll'
                required.append(directories[0].relative_to(source) / dll)
            runtime_copies.extend((file, source / runtime / file.name)
                                  for file in directories[0].glob('*.dll'))
        environments[arch] = {'env': {
            'VSINSTALLDIR': [['.']], 'VCINSTALLDIR': [['VC']],
            'VCToolsInstallDir': [list(vc.parts)],
            'PATH': [list(p.parts) for p in bins],
            'INCLUDE': [list(p.parts) for p in includes],
            'LIB': [list(p.parts) for p in libs],
        }}
    for relative in required:
        if not (source / relative).is_file():
            raise ValueError(f'Windows toolchain is incomplete: {relative}')
    for file, target in runtime_copies:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
    (source / 'VS_VERSION').write_text(vs_version + '\n')
    for arch, env in environments.items():
        (source / sdk / 'bin' / f'SetEnv.{arch}.json').write_text(json.dumps(env, indent=2) + '\n')
    files = sorted((p for p in source.rglob('*') if p.is_file()),
                   key=lambda p: p.relative_to(source).as_posix().replace('/', '\\').lower())
    # This is depot_tools' CalculateHash('vs_files', None) format.
    digest = hashlib.sha1()
    for file in files:
        relative = file.relative_to(source).as_posix()
        digest.update(('vs_files\\' + relative.replace('/', '\\')).lower().encode())
        with file.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
    archives.mkdir(parents=True, exist_ok=True)
    archive = archives / (digest.hexdigest()[:10] + '.zip')
    if not archive.exists():
        temporary = archive.with_suffix('.partial')
        try:
            with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as zipped:
                for file in files:
                    zipped.write(file, file.relative_to(source).as_posix())
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--vs-version', required=True)
    parser.add_argument('--sdk-version', required=True)
    parser.add_argument('--accept-license', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Automatic Windows toolchain preparation requires macOS.')
    if not args.accept_license:
        parser.error('--accept-license is required for the Microsoft build tools and SDK.')
    if args.vs_version != '2026' or args.sdk_version != SDK_VERSION:
        parser.error('Automatic setup currently supports VS 2026 / SDK 10.0.26100.0. Use configure for another version.')
    if not all(shutil.which(tool) for tool in ['msiextract', '7zz']):
        parser.error('Install the macOS extraction tools first: brew install msitools sevenzip')
    workspace = args.root.resolve() / '.dao/windows-sdk'
    workspace.mkdir(parents=True, exist_ok=True)
    probe = workspace / 'case-check'
    probe.write_text('')
    insensitive = probe.with_name('CASE-CHECK').exists()
    probe.unlink()
    if not insensitive:
        parser.error('Place this checkout and depot_tools on a case-insensitive macOS volume.')
    cache = workspace / 'cache'
    downloader = cache / ('vsdownload-' + DOWNLOADER_REVISION + '.py')
    download_verified(f'https://raw.githubusercontent.com/mstorsjo/msvc-wine/{DOWNLOADER_REVISION}/vsdownload.py',
                      downloader, DOWNLOADER_SHA256)
    manifest = cache / 'visual-studio.manifest'
    download_verified(VS_MANIFEST_URL, manifest, VS_MANIFEST_SHA256)
    source = workspace / 'vs'
    subprocess.run([sys.executable, str(downloader), '--accept-license', '--manifest', str(manifest),
                    '--host-arch', 'x64', '--architecture', 'x86', 'x64', 'arm64', '--sdk-version', '10.0.26100',
                    '--with-workload', 'no', '--with-msbuild', 'no', '--with-devcmd', 'no',
                    '--with-asan', 'no', '--skip-patch', '--dest', str(source), '--cache', str(cache / 'vs')], check=True)
    install_debuggers(cache, source)
    print('Validating and packaging the Windows toolchain...', flush=True)
    archive = package_toolchain(source, workspace / 'archives', args.vs_version, args.sdk_version)
    result = workspace / 'bootstrap.json'
    temporary = result.with_suffix('.partial')
    temporary.write_text(json.dumps({'baseUrl': str(archive.parent), 'hash': archive.stem}) + '\n')
    temporary.replace(result)
    print(f'Windows toolchain archive ready: {archive}', flush=True)


if __name__ == '__main__':
    main()
