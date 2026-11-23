#!/usr/bin/env python3
"""Rebuild the checked-in SPIR-V header; requires glslangValidator and spirv-val."""
from pathlib import Path
import struct
import subprocess
import tempfile
root=Path(__file__).resolve().parent
output='/* Generated from shaders/composite.{vert,frag} with glslangValidator -V. */\n'
with tempfile.TemporaryDirectory() as directory:
    for stage in ('vert','frag'):
        binary=Path(directory)/stage
        subprocess.run(['glslangValidator','-V','--target-env','vulkan1.0',str(root/('composite.'+stage)),'-o',str(binary)],check=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.0',str(binary)],check=True)
        data=binary.read_bytes();words=struct.unpack('<%dI'%(len(data)//4),data)
        output+='static const uint32_t vp_'+stage+'_spv[]={\n'+''.join('    '+','.join('0x%08x'%x for x in words[i:i+8])+',\n' for i in range(0,len(words),8))+'};\n'
(root.parent/'composite_spv.h').write_text(output)
