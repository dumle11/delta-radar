#!/usr/bin/env python3
"""Index local SDK leads for visible-surface fill; never connect to a device.

Offsets describe the bundled dump, not validated live layouts or callable code.
Use --check to verify the committed index after a source/SDK update.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "docs/ENEMY_FILL_SDK_INDEX.json"

# Deliberately scoped leads, not an exhaustive inventory of visibility systems.
TARGETS = {
    "Engine.Actor": ("WasRecentlyRendered",),
    "MetaDorian.MTAPI_UPrimitiveComponent": (
        "GetLastRenderTime", "GetLastRenderTimeOnScreen"),
    "DFMGameplay.DFMPlayerController": (
        "LineOfSightToCharacter", "LineOfSightToLocation"),
    "Engine.SkinnedMeshComponent": (
        "EfffectPassMaterial", "bDrawEffectPass", "bEffectPassNeedDepth",
        "bDrawSecondPassMaterial", "SecondPassMaterials",
        "SetMultiDrawEffect", "SetSecondPassMaterial"),
    "GPGameplay.GPOutLineEffectComponent": (
        "OutLineRed", "ActiveOutlineMeshList",
        "EnableOutlineHDStencilMaskIgnoreDepth", "PlayOutLineEffect"),
    "DFMGameplay.DFMCharacter": ("DFMOutLineEffectComponent",),
    "Engine.PrimitiveComponent": (
        "bRenderCustomDepth", "CustomDepthStencilWriteMask",
        "CustomDepthStencilValue"),
    "Engine.Material": ("bDisableDepthTest",),
}


def build_index():
    candidates = []
    hashes = {}
    packages = sorted({name.split(".")[0] for name in TARGETS})
    for package in packages:
        path = ROOT / f"CppSDK/SDK/{package}_classes.hpp"
        raw = path.read_bytes()
        relative = path.relative_to(ROOT).as_posix()
        hashes[relative] = hashlib.sha256(raw).hexdigest()
        owner = None
        function = None
        native_offset = None
        for line_number, line in enumerate(raw.decode("utf-8-sig").splitlines(), 1):
            if line.startswith("// Class "):
                owner = line.removeprefix("// Class ").strip()
                function = native_offset = None
            if owner not in TARGETS:
                continue
            fn = re.search(r"// Function (\S+)", line)
            if fn:
                function = fn[1]
                native_offset = None
            native = re.search(r"NativeExecOffset\((0x[0-9A-Fa-f]+)\)", line)
            if native:
                native_offset = native[1]
            declaration = line.split("//", 1)[0].strip()
            if not declaration.endswith(";"):
                continue
            for member in TARGETS[owner]:
                if not re.search(r"\b" + re.escape(member) + r"\b", declaration):
                    continue
                method = re.search(r"\b" + re.escape(member) + r"\s*\(", declaration)
                record = {
                    "owner": owner, "member": member,
                    "kind": "function" if method else "field",
                    "source": relative, "line": line_number,
                    "declaration": declaration,
                    "liveValidated": False,
                }
                if method:
                    if function != f"{owner}.{member}" or native_offset is None:
                        raise ValueError(f"Missing native metadata: {owner}.{member}")
                    record["nativeExecOffset"] = native_offset
                else:
                    offset = re.search(r"// (0x[0-9A-Fa-f]+)\(", line)
                    if offset is None:
                        raise ValueError(f"Missing field metadata: {owner}.{member}")
                    record["fieldOffset"] = offset[1]
                    bit = re.search(r"BitIndex: (0x[0-9A-Fa-f]+)", line)
                    if bit:
                        record["bitIndex"] = int(bit[1], 16)
                candidates.append(record)
    expected = {(owner, member) for owner, members in TARGETS.items() for member in members}
    found = [(item["owner"], item["member"]) for item in candidates]
    if set(found) != expected or len(found) != len(expected):
        raise ValueError(f"SDK index mismatch: missing={sorted(expected - set(found))}; "
                         f"duplicateCount={len(found) - len(set(found))}")
    return {
        "schemaVersion": 1,
        "status": "offline SDK declarations only; no working fill implementation",
        "limitations": [
            "No device access or game execution is performed by this tool.",
            "Native exec offsets identify dumped entry points, not verified function bodies.",
            "Field offsets and bit indices are unverified on the running mobile build.",
            "Visibility timestamps and line-of-sight results are not per-pixel masks.",
        ],
        "sourceSha256": hashes,
        "candidates": sorted(candidates, key=lambda item: (item["owner"], item["member"])),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="Compare without writing")
    args = parser.parse_args()
    index = build_index()
    rendered = json.dumps(index, indent=2) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text() != rendered:
            parser.exit(1, f"SDK index is missing or outdated: {args.output}\n")
    else:
        args.output.write_text(rendered)
    print(f"{'Verified' if args.check else 'Indexed'} {len(index['candidates'])} offline SDK leads")


if __name__ == "__main__":
    main()
