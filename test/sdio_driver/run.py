"""Exercise the actual pinned SDK function bodies under deterministic ISR schedules."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import subprocess


def extract(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("sdk_source", type=Path, help="Installed SdCard/SdioTeensy.cpp")
parser.add_argument("--cxx", default="c++")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
adapter = runpy.run_path(str(root.parents[1] / "script/sdio_sdk.py"))
original = args.sdk_source.read_text(encoding="utf-8")
fixed = adapter["apply"](original)
try:
    adapter["apply"](original + "\n// SDK changed\n")
except RuntimeError:
    pass
else:
    raise AssertionError("An unreviewed SDK source must fail before compilation")

template = (root / "model.cpp.in").read_text()
signatures = json.loads((root / "functions.json").read_text())
args.output.mkdir(parents=True, exist_ok=True)
for name, source in (("original", original), ("fixed", fixed)):
    functions = "\n".join(extract(source, signature) for signature in signatures)
    # The native model supplies the IRQ mask; all other driver statements remain exact.
    functions = functions.replace(
        '__asm__ volatile("mrs %0, primask" : "=r"(primask));',
        "primask=__get_PRIMASK();",
    )
    generated = args.output / (name + ".cpp")
    generated.write_text(template.replace("@DRIVER@", functions), encoding="utf-8")
    executable = args.output / (name + ".exe")
    subprocess.run([args.cxx, "-std=c++17", "-O0", "-g", "-UNDEBUG",
                    "-DFIXED=" + str(int(name == "fixed")), str(generated),
                    "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
    (args.output / (name + ".log")).write_text(result.stdout)
    print(name + ":\n" + result.stdout, end="")

(args.output / "result.json").write_text(json.dumps({
    "passed": True,
    "original_normalized_sha256": hashlib.sha256(original.encode()).hexdigest(),
    "fixed_normalized_sha256": hashlib.sha256(fixed.encode()).hexdigest(),
    "scope": "W1C registers, two adverse ISR schedules, real errors, stale completion and nested IRQ mask; virtual time, not CPU performance",
}, indent=2))
