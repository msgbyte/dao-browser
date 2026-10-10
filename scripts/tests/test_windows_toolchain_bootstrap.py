import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / 'bootstrap-windows-toolchain.py'
SPEC = importlib.util.spec_from_file_location('bootstrap_windows_toolchain', SCRIPT)
BOOTSTRAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOOTSTRAP)


class WindowsToolchainBootstrapTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dao sdk \u96ea ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.archives = self.root / 'archives'
        self.sdk = 'Windows Kits/10'
        self.version = '10.0.26100.0'
        self.vc = 'VC/Tools/MSVC/14.50.35717'
        self.write(f'{self.vc}/include/vector')
        self.write(f'{self.vc}/atlmfc/include/atlbase.h')
        self.write('DIA SDK/bin/amd64/msdia140.dll')
        for section in ['um', 'shared', 'winrt', 'ucrt']:
            self.write(f'{self.sdk}/Include/{self.version}/{section}/header.h')
        self.write(f'{self.sdk}/Include/{self.version}/um/Windows.h')
        for arch in ['x86', 'x64', 'arm64']:
            self.write(f'{self.vc}/bin/Hostx64/{arch}/cl.exe')
            self.write(f'{self.vc}/lib/{arch}/libcmt.lib')
            self.write(f'{self.vc}/atlmfc/lib/{arch}/atls.lib')
            self.write(f'{self.sdk}/Lib/{self.version}/um/{arch}/kernel32.lib')
            self.write(f'{self.sdk}/Lib/{self.version}/ucrt/{arch}/ucrt.lib')
            self.write(f'{self.sdk}/bin/{self.version}/{arch}/rc.exe')
            self.write(f'{self.sdk}/bin/{self.version}/{arch}/ucrt/ucrtbased.dll')
            self.write(f'{self.sdk}/Debuggers/{arch}/dbghelp.dll')
            for dll in ['msvcp140', 'msvcp140_atomic_wait', 'vcruntime140', 'vccorlib140', 'vcruntime140_1']:
                self.write(f'VC/Redist/MSVC/14.50.35717/{arch}/Microsoft.VC145.CRT/{dll}.dll')
                debug_dll = 'msvcp140d_atomic_wait' if dll == 'msvcp140_atomic_wait' else dll + 'd'
                self.write(f'VC/Redist/MSVC/14.50.35717/debug_nonredist/{arch}/Microsoft.VC145.DebugCRT/{debug_dll}.dll')

    def write(self, relative):
        file = self.source / relative
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(b'fixture')

    def test_packages_chromium_layout_and_content_hash(self):
        archive = BOOTSTRAP.package_toolchain(self.source, self.archives, '2026', self.version)
        with zipfile.ZipFile(archive) as zipped:
            names = zipped.namelist()
            self.assertEqual(zipped.read('VS_VERSION'), b'2026\n')
            self.assertIn('sys64/msvcp140.dll', names)
            self.assertIn('sys32/msvcp140d.dll', names)
            env = json.loads(zipped.read(f'{self.sdk}/bin/SetEnv.x64.json'))['env']
            for key in ['PATH', 'INCLUDE', 'LIB']:
                self.assertTrue(env[key])
                for parts in env[key]:
                    self.assertTrue((self.source.joinpath(*parts)).is_dir())
            self.assertIn(self.vc.split('/') + ['atlmfc', 'include'], env['INCLUDE'])
            arm_env = json.loads(zipped.read(f'{self.sdk}/bin/SetEnv.arm64.json'))['env']
            self.assertIn(self.vc.split('/') + ['lib', 'arm64'], arm_env['LIB'])
            digest = hashlib.sha1()
            for name in sorted(names, key=lambda name: name.replace('/', '\\').lower()):
                digest.update(('vs_files\\' + name.replace('/', '\\')).lower().encode())
                digest.update(zipped.read(name))
            self.assertEqual(archive.stem, digest.hexdigest()[:10])
        self.assertEqual(BOOTSTRAP.package_toolchain(self.source, self.archives, '2026', self.version), archive)

    def test_rejects_incomplete_sdk_before_publishing_archive(self):
        (self.source / self.sdk / 'Debuggers/x64/dbghelp.dll').unlink()
        with self.assertRaisesRegex(ValueError, 'dbghelp.dll'):
            BOOTSTRAP.package_toolchain(self.source, self.archives, '2026', self.version)
        self.assertEqual(list(self.archives.glob('*.zip')), [])

    def test_rejects_symlinks_outside_the_toolchain(self):
        (self.source / 'escaped').symlink_to(self.root)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            BOOTSTRAP.package_toolchain(self.source, self.archives, '2026', self.version)

    def test_rejects_cached_downloader_with_wrong_checksum(self):
        file = self.root / 'vsdownload.py'
        file.write_text('corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            BOOTSTRAP.download_verified('https://example.invalid/tool', file, '0' * 64)

    def test_selects_only_debugger_payloads_from_the_pinned_sdk_manifest(self):
        manifest = ET.fromstring('''<BurnManifest xmlns="http://schemas.microsoft.com/wix/2008/Burn">
          <Payload Id="debug" FilePath="Installers/SDK Debuggers-x86_en-us.msi" Hash="abc"/>
          <Payload Id="cab" FilePath="Installers/debug.cab" Hash="def"/>
          <Payload Id="other" FilePath="Installers/other.cab" Hash="ghi"/>
          <Chain><MsiPackage Id="package_SDKDebuggers_x86_en_us">
            <PayloadRef Id="debug"/><PayloadRef Id="cab"/>
          </MsiPackage></Chain>
        </BurnManifest>''')
        self.assertEqual(BOOTSTRAP.debugger_payloads(manifest), [
            (Path('Installers/SDK Debuggers-x86_en-us.msi'), 'abc'),
            (Path('Installers/debug.cab'), 'def'),
        ])
        manifest[0].set('FilePath', '../outside.msi')
        with self.assertRaisesRegex(ValueError, 'Unsafe SDK payload'):
            BOOTSTRAP.debugger_payloads(manifest)


if __name__ == '__main__':
    unittest.main()
