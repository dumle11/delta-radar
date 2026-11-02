#!/usr/bin/env python3
"""Regenerate exact-build native class anchors from the SDK shipped in this repo."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
# The native registration range was checked against character, capsule and
# camera inheritance chains in the live 1.202.37117.5601 process. Dynamic
# Blueprint indices are process dependent and must never be used as anchors.
LAST_NATIVE_INDEX = 11000
FRAGMENTS = (
    'InventoryPickup', 'ChangeableContainer', 'SingleItemContainer',
    'GPCharacterBase', 'GPCharacter', 'GPPlayerCameraManager', 'PlayerCameraManager',
    'GPHealthDataComponent', 'GPAttributeSetHealth', 'CapsuleComponent',
    'SkeletalMeshComponent', 'SkeletalMesh', 'PickupBase', 'PlunderPickup',
    'GPBattleFieldSystem',
)

def main():
    indices = {}
    for line in (ROOT/'GObjects-Dump.txt').read_text().splitlines():
        match = re.match(r'\[([0-9A-F]+)\] \{[^}]+\} Class (\S+)', line)
        if match:
            indices[match[2]] = int(match[1], 16)
    classes, symbols = {}, {}
    for path in sorted((ROOT/'CppSDK/SDK').glob('*_classes.hpp')):
        source = path.read_text(encoding='utf-8-sig')
        for match in re.finditer(r'^// Class (\S+)\n(?:(?!^// Class ).)*?^class ([^\n]+)', source, re.M|re.S):
            if ' : public ' not in match[2]:
                continue
            left, parent = match[2].split(' : public ', 1)
            symbol = left.replace(' final', '').split()[-1]
            classes[match[1]] = parent.split()[0]
            symbols[symbol] = match[1]
    rows = []
    for name, index in indices.items():
        short_name = name.split('.')[-1]
        parent = indices.get(symbols.get(classes.get(name)))
        if index <= LAST_NATIVE_INDEX and parent is not None and any(s in short_name for s in FRAGMENTS):
            rows.append((index, parent, short_name))
    header = ('/* Generated from the bundled Proxima 1.202.37117.5601 SDK and object dump.\n'
              ' * Native class index AND immediate superclass index must match.\n'
              ' * Dynamic Blueprint object indices are deliberately excluded. */\n')
    (ROOT/'scanner/sdk_native_classes.inc').write_text(header + ''.join(
        f'    {{{index}, {parent}, "{name}"}},\n' for index, parent, name in sorted(rows)))
    print(f'Generated {len(rows)} native class anchors')

if __name__ == '__main__':
    main()
