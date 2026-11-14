"""Build/run the production DLSS-G preflight with a fake Streamline DLL, without NVIDIA hardware or game data.

Windows: --cc <clang> --deps <Vulkan include directory> [--streamline-sdk <SDK root>].
Without the SDK, still tests the normal build's fallback and non-NVIDIA guard. No downloads.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    ap.add_argument('--deps', type=Path, required=True)
    ap.add_argument('--streamline-sdk', type=Path)
    args = ap.parse_args()
    if os.name != 'nt':
        ap.error('the DLL lifecycle fixture currently requires Windows')
    out = ROOT / 'build/tests/dlssg-preflight'
    out.mkdir(parents=True, exist_ok=True)
    count = 0
    for opt in ('-O0', '-O2'):
        base = [args.cc, '-std=c++20', opt, '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'runtime'), '-I', str(args.deps)]
        sources = [str(ROOT / 'runtime/vk_dlssg.cpp'), str(ROOT / 'tests/dlssg_preflight_test.cpp')]
        plain = out / f'plain-{opt[1:]}.exe'
        subprocess.run(base + sources + ['-o', str(plain)], check=True)

        def run(exe, case, expected, message, events=()):
            nonlocal count
            trace = exe.parent / 'events.txt'
            trace.unlink(missing_ok=True)
            env = dict(os.environ, DKB_TEST_DLSSG_CASE=case, DKB_TEST_DLSSG_EVENTS=str(trace))
            result = subprocess.run([str(exe), case], env=env, capture_output=True, text=True, timeout=20, check=True)
            assert f'results: {expected}' in result.stdout, result.stdout + result.stderr
            assert result.stdout.count(message) == 2, result.stdout  # once per device, even with two calls
            actual = trace.read_text().splitlines() if trace.exists() else []
            assert actual == list(events), (case, actual, events)
            count += 1

        run(plain, 'plain', '0 0', 'Streamline support was not built')
        run(plain, 'amd', '0 0', 'requires a selected NVIDIA')
        run(plain, 'null', '0 0', 'requires a selected NVIDIA')
        if not args.streamline_sdk:
            continue
        sdk_flags = ['-DDKB_HAVE_STREAMLINE=1', '-I', str(args.streamline_sdk / 'include')]
        sdk_exe = out / f'sdk-{opt[1:]}.exe'
        subprocess.run(base + sdk_flags + sources + ['-o', str(sdk_exe)], check=True)
        fixture = out / f'fixture-{opt[1:]}.dll'
        subprocess.run(base + sdk_flags + ['-shared', str(ROOT / 'tests/dlssg_streamline_fixture.cpp'),
                                           '-o', str(fixture)], check=True)
        missing_export = out / f'missing-export-{opt[1:]}.dll'
        subprocess.run(base + sdk_flags + ['-DDKB_TEST_NO_EXPORT', '-shared',
                                           str(ROOT / 'tests/dlssg_streamline_fixture.cpp'),
                                           '-o', str(missing_export)], check=True)
        cases = [
            ('missing', '0 0', 'could not load', [], None),
            ('export', '0 0', 'lacks a required preflight export', [], missing_export),
            ('requirements', '0 0', 'feature requirements unavailable', ['init', 'requirements', 'shutdown'] * 2, fixture),
            ('no-vulkan', '0 0', 'does not support DLSS-G on Vulkan', ['init', 'requirements', 'shutdown'] * 2, fixture),
            ('driver', '0 0', 'NVIDIA driver is too old', ['init', 'requirements', 'support', 'shutdown'] * 2, fixture),
            ('hws', '0 0', 'GPU scheduling is disabled', ['init', 'requirements', 'support', 'shutdown'] * 2, fixture),
            ('adapter', '0 0', 'does not support DLSS-G', ['init', 'requirements', 'support', 'shutdown'] * 2, fixture),
            ('success', '1 1', 'preflight passed', ['init', 'requirements', 'support', 'shutdown'] * 2, fixture),
            ('amd', '0 0', 'requires a selected NVIDIA', [], fixture),
            ('null', '0 0', 'requires a selected NVIDIA', [], fixture),
        ]
        for case, expected, message, events, dll in cases:
            folder = out / f'{opt[1:]}-{case}'
            folder.mkdir(exist_ok=True)
            exe = folder / 'probe.exe'
            shutil.copy2(sdk_exe, exe)
            target = folder / 'sl.interposer.dll'
            target.unlink(missing_ok=True)
            if dll:
                shutil.copy2(dll, target)
            run(exe, case, expected, message, events)
        # Failed init/shutdown retains the module and forbids retries, including after a device recreation.
        for case, events in [('init', ['init']), ('shutdown', ['init', 'requirements', 'support', 'shutdown'])]:
            folder = out / f'{opt[1:]}-{case}'
            folder.mkdir(exist_ok=True)
            exe = folder / 'probe.exe'
            shutil.copy2(sdk_exe, exe)
            shutil.copy2(fixture, folder / 'sl.interposer.dll')
            trace = folder / 'events.txt'
            trace.unlink(missing_ok=True)
            env = dict(os.environ, DKB_TEST_DLSSG_CASE=case, DKB_TEST_DLSSG_EVENTS=str(trace))
            r = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=20, check=True)
            assert 'results: 0 0' in r.stdout and 'restart the application' in r.stdout, r.stdout
            assert trace.read_text().splitlines() == events, r.stdout
            count += 1
    print(f'DLSS-G preflight: {count} cases passed (-O0 and -O2)')


if __name__ == '__main__':
    main()
