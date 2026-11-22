#!/usr/bin/env python3
"""Sample one browser process tree using macOS kernel resource counters."""
import ctypes
import json
import statistics
import subprocess
import sys
import time

# rusage_info_v0 from the macOS SDK's sys/resource.h (nanoseconds and bytes).
class Usage(ctypes.Structure):
    _fields_ = [("uuid", ctypes.c_uint8 * 16)] + [
        (name, ctypes.c_uint64) for name in (
            "user", "system", "idle_wakeups", "interrupt_wakeups", "pageins",
            "wired", "resident", "footprint", "started", "exited")]

lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
lib.proc_pid_rusage.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
lib.proc_pid_rusage.restype = ctypes.c_int
root, duration = int(sys.argv[1]), float(sys.argv[2])

def snapshot():
    rows = subprocess.check_output(['ps', '-A', '-o', 'pid=,ppid='], text=True)
    children = {}
    for row in rows.splitlines():
        pid, parent = map(int, row.split())
        children.setdefault(parent, []).append(pid)
    pids, index = [root], 0
    while index < len(pids):
        pids.extend(children.get(pids[index], []))
        index += 1
    result = {}
    for pid in pids:
        usage = Usage()
        if lib.proc_pid_rusage(pid, 0, ctypes.byref(usage)) == 0:
            result[(pid, usage.started)] = usage
    if not any(pid == root for pid, _ in result):
        raise RuntimeError('Browser process exited or its counters are inaccessible')
    return result

start = time.monotonic()
previous = snapshot()
samples = []
while time.monotonic() - start < duration:
    before = time.monotonic()
    time.sleep(min(10, max(0, duration - (before - start))))
    current = snapshot()
    elapsed = time.monotonic() - before
    shared = current.keys() & previous.keys()
    cpu = sum((current[k].user + current[k].system -
               previous[k].user - previous[k].system) / 1e9 for k in shared)
    idle = sum(current[k].idle_wakeups - previous[k].idle_wakeups for k in shared)
    interrupts = sum(current[k].interrupt_wakeups - previous[k].interrupt_wakeups for k in shared)
    browser_key = next(k for k in shared if k[0] == root)
    browser = current[browser_key]
    old_browser = previous[browser_key]
    samples.append({
        'seconds': elapsed, 'cpuSeconds': cpu, 'idleWakeups': idle,
        'interruptWakeups': interrupts, 'processes': len(current),
        'browserCpuSeconds': (browser.user + browser.system -
                              old_browser.user - old_browser.system) / 1e9,
        'browserRssMiB': browser.resident / 2**20,
        'browserFootprintMiB': browser.footprint / 2**20,
        'rssMiB': sum(v.resident for v in current.values()) / 2**20,
        'footprintMiB': sum(v.footprint for v in current.values()) / 2**20,
        'newProcesses': len(current.keys() - previous.keys()),
        'exitedProcesses': len(previous.keys() - current.keys()),
    })
    previous = current
elapsed = sum(s['seconds'] for s in samples)
print(json.dumps({
    'seconds': elapsed,
    'cpuPercentOneCore': sum(s['cpuSeconds'] for s in samples) / elapsed * 100,
    'browserCpuPercentOneCore': sum(s['browserCpuSeconds'] for s in samples) / elapsed * 100,
    'medianBrowserRssMiB': statistics.median(s['browserRssMiB'] for s in samples),
    'medianBrowserFootprintMiB': statistics.median(s['browserFootprintMiB'] for s in samples),
    'idleWakeupsPerSecond': sum(s['idleWakeups'] for s in samples) / elapsed,
    'interruptWakeupsPerSecond': sum(s['interruptWakeups'] for s in samples) / elapsed,
    'medianRssMiB': statistics.median(s['rssMiB'] for s in samples),
    'medianFootprintMiB': statistics.median(s['footprintMiB'] for s in samples),
    'samples': samples,
}))
