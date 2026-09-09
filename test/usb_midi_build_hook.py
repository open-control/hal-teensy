"""SDK drift and wrapper/middleware regression tests; no PlatformIO required."""

import hashlib
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch


hook = runpy.run_path(str(Path(__file__).resolve().parents[1] / "script/usb_midi_sdk.py"))


class BuildEnvironment:
    def __init__(self, root):
        self.root = root
        self.middleware = None

    def PioPlatform(self):
        return self

    def get_package_dir(self, name):
        assert name == "framework-arduinoteensy"
        return self.root / "sdk"

    def subst(self, name):
        assert name == "$BUILD_DIR"
        return str(self.root / "build")

    def File(self, path):
        return Path(path)

    def AddBuildMiddleware(self, callback, pattern):
        assert pattern == "*usb_midi.c"
        self.middleware = callback


class SourceNode:
    def __init__(self, path):
        self.path = path

    def srcnode(self):
        return self

    def get_abspath(self):
        return str(self.path)


class UsbMidiBuildHookTests(unittest.TestCase):
    def test_sdk_change_is_rejected_before_writes(self):
        for changed in hook["SDK_SOURCES"]:
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as tmp:
                env = BuildEnvironment(Path(tmp))
                source = env.root / "sdk/cores/teensy4/usb_midi.c"
                source.parent.mkdir(parents=True)
                digest = hashlib.sha256(b"reviewed SDK\n").hexdigest()
                hashes = dict.fromkeys(hook["SDK_SOURCES"], digest)
                for name in hashes:
                    source.with_name(name).write_bytes(b"reviewed SDK\n")
                source.with_name(changed).write_bytes(b"unreviewed SDK\n")
                with patch.dict(hook["configure"].__globals__, SDK_SOURCES=hashes):
                    with self.assertRaisesRegex(RuntimeError, "needs SDK review"):
                        hook["configure"](env)
                self.assertFalse((env.root / "build").exists())
                self.assertEqual(source.with_name(changed).read_bytes(), b"unreviewed SDK\n")

    def test_only_verified_source_is_replaced_without_changing_sdk(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = BuildEnvironment(Path(tmp))
            source = env.root / "sdk/cores/teensy4/usb_midi.c"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"reviewed SDK\r\n")
            source.with_name("usb.c").write_bytes(b"reviewed SDK\r\n")
            digest = hashlib.sha256(b"reviewed SDK\n").hexdigest()
            hashes = dict.fromkeys(hook["SDK_SOURCES"], digest)
            with patch.dict(hook["configure"].__globals__, SDK_SOURCES=hashes):
                hook["configure"](env)
                wrapper = env.root / "build/oc_usb_midi_sdk.c"
                stamp = wrapper.stat().st_mtime_ns
                hook["configure"](env)
            self.assertEqual(wrapper.stat().st_mtime_ns, stamp)
            self.assertEqual(source.read_bytes(), b"reviewed SDK\r\n")
            self.assertEqual(source.with_name("usb.c").read_bytes(), b"reviewed SDK\r\n")
            self.assertEqual(env.middleware(env, SourceNode(source)), wrapper)
            other = SourceNode(env.root / "other/usb_midi.c")
            self.assertIs(env.middleware(env, other), other)
            text = wrapper.read_text()
            self.assertIn("#define usb_midi_configure oc_usb_midi_sdk_configure", text)
            self.assertIn("#define usb_midi_flush_output oc_usb_midi_sdk_flush_output", text)
            self.assertIn("#undef usb_midi_flush_output", text)
            self.assertIn(source.as_posix(), text)
            self.assertIn("UsbMidiTx.inc", text)


if __name__ == "__main__":
    unittest.main()
