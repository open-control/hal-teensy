"""PRE build hook: extend one verified SDK source, never patch the package cache."""

import hashlib
import inspect
from pathlib import Path


# Teensyduino 1.62 / PlatformIO framework-arduinoteensy 1.162.0. Private DMA
# state is deliberately version-locked: an SDK upgrade requires review/tests.
SDK_SOURCES = {
    "usb_midi.c": "ee885657eddc165205df08d27b85bc397988f37551e660f75d0a2414145ac048",
    # Descriptor retirement must precede the completion callback.
    "usb.c": "8cb03e83e90527e5574c7977bc8957edbc16f27c2b210c7949a507158f8bf78c",
}


def configure(env):
    sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy"))
    source = sdk / "cores/teensy4/usb_midi.c"
    for name, expected in SDK_SOURCES.items():
        checked = source.with_name(name)
        digest = hashlib.sha256(checked.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        if digest != expected:
            raise RuntimeError(f"USB MIDI adapter needs SDK review: {checked} sha256={digest}")
    root = Path(inspect.getfile(configure)).resolve().parent.parent
    extension = root / "src/oc/hal/teensy/detail/UsbMidiTx.inc"
    wrapper = Path(env.subst("$BUILD_DIR")) / "oc_usb_midi_sdk.c"
    contents = (
        '#define usb_midi_configure oc_usb_midi_sdk_configure\n'
        '#define usb_midi_flush_output oc_usb_midi_sdk_flush_output\n'
        f'#include "{source.as_posix()}"\n'
        '#undef usb_midi_configure\n'
        '#undef usb_midi_flush_output\n'
        f'#include "{extension.as_posix()}"\n'
    )
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    if not wrapper.exists() or wrapper.read_text() != contents:
        wrapper.write_text(contents, encoding="utf-8", newline="\n")

    def replace_midi_source(env, node):
        if Path(node.srcnode().get_abspath()).resolve() == source.resolve():
            return env.File(str(wrapper))
        return node

    env.AddBuildMiddleware(replace_midi_source, "*usb_midi.c")


if "Import" in globals():
    Import("env")
    configure(env)
