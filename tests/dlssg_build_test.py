"""DLSS-G build integration: SDK flags belong only to the bridge; invalid/prebuilt configurations fail early."""
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_game as build


class StreamlineBuild(unittest.TestCase):
    def ninja(self, sdk=None):
        sources = [build.RUNTIME / 'vk_dlssg.cpp', build.RUNTIME / 'vk_framegen.c', build.RUNTIME / 'nvn2_lower.cpp']
        with patch.object(build, 'DEPS', ROOT / 'build/tests/deps'), patch.object(build, 'runtime_sources', return_value=sources):
            text, _, _ = build.ninja_file(ROOT / 'generated/test', ROOT / 'build/tests/dlssg-build', {}, 1,
                                           make_lib=True, streamline_sdk=sdk)
        return text

    def test_default_build_has_no_sdk_dependency(self):
        text = self.ninja()
        self.assertIn('vk_dlssg.cpp', text)
        self.assertNotIn('DKB_HAVE_STREAMLINE', text)
        self.assertNotIn('sl.interposer', text)

    def test_sdk_flags_only_apply_to_bridge(self):
        sdk = ROOT / 'build/tests/SDK with spaces'
        lines = self.ninja(sdk).splitlines()
        enabled = [i for i, line in enumerate(lines) if 'DKB_HAVE_STREAMLINE' in line]
        self.assertEqual(len(enabled), 1)
        self.assertIn('vk_dlssg.cpp', lines[enabled[0] - 1])
        self.assertIn(build.cmd_path(sdk / 'include'), lines[enabled[0]])
        self.assertNotIn('sl.interposer', '\n'.join(lines))

    def test_invalid_sdk_fails_before_build_work(self):
        with patch.object(build, 'WIN', True), patch.object(sys, 'argv', ['build_game.py', '--streamline-sdk',
                str(ROOT / 'build/tests/nonexistent-streamline-sdk')]), contextlib.redirect_stderr(io.StringIO()) as err:
            with self.assertRaises(SystemExit) as ex:
                build.main()
        self.assertEqual(ex.exception.code, 2)
        self.assertIn('--streamline-sdk: missing', err.getvalue())

    def test_prebuilt_runtime_rejects_ineffective_sdk_flag(self):
        with patch.object(build, 'WIN', True), patch.object(sys, 'argv', ['build_game.py', '--streamline-sdk', '.',
                '--runtime-lib', 'prebuilt.a']), contextlib.redirect_stderr(io.StringIO()) as err:
            with self.assertRaises(SystemExit) as ex:
                build.main()
        self.assertEqual(ex.exception.code, 2)
        self.assertIn('not with --runtime-lib', err.getvalue())

    def test_non_windows_sdk_rejected(self):
        with patch.object(build, 'WIN', False), patch.object(sys, 'argv', ['build_game.py', '--streamline-sdk', '.']), \
                contextlib.redirect_stderr(io.StringIO()) as err:
            with self.assertRaises(SystemExit) as ex:
                build.main()
        self.assertEqual(ex.exception.code, 2)
        self.assertIn('Windows only', err.getvalue())


if __name__ == '__main__':
    unittest.main()
