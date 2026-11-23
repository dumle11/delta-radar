#!/usr/bin/env python3
"""Regression for the PC-relative BL in the actual Adreno end-pass prologue."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "chams_reloc.h"
int main(void) {
    uint32_t out[16]; uintptr_t address;
    uint32_t actual[]={0xa9bf7bfd,0x910003fd,0x94016db6,0xa8c17bfd};
    assert(cr_trampoline(out,actual,0x9fb48)==48);
    assert(out[0]==actual[0] && out[1]==actual[1]);
    assert(out[2]==0x58000070 && out[3]==0xd63f0200 && out[4]==0x14000003);
    memcpy(&address,&out[5],8); assert(address==0xfb228);
    assert(out[7]==actual[3]);
    memcpy(&address,&out[10],8); assert(address==0x9fb58);
    /* Negative branch displacement and ordinary copied prologues. */
    uint32_t backward[]={0x97fffffc,0xd503201f,0xd503201f,0xd503201f};
    assert(cr_trampoline(out,backward,0x1000)==48);
    memcpy(&address,&out[3],8); assert(address==0xff0);
    uint32_t plain[]={0xd10103ff,0xf90013f3,0xa9037bfd,0x9100c3fd};
    assert(cr_trampoline(out,plain,0x1000)==32 && !memcmp(plain,out,16));
    uint32_t adr[]={0x90000000,0xd503201f,0xd503201f,0xd503201f};
    assert(cr_trampoline(out,adr,0x1234)==44);
    uint64_t decoded=0;
    for(unsigned k=0;k<4;k++) decoded|=(uint64_t)((out[k]>>5)&65535)<<(k*16);
    assert(decoded==0x1000);
    adr[0]=0x10000000; assert(cr_trampoline(out,adr,0x1234)==44);
    decoded=0;for(unsigned k=0;k<4;k++) decoded|=(uint64_t)((out[k]>>5)&65535)<<(k*16);
    assert(decoded==0x1234);
    uint32_t unsupported[]={0x58000000,0,0,0}; assert(!cr_trampoline(out,unsupported,0x1000));
    unsupported[0]=0x54000000; assert(!cr_trampoline(out,unsupported,0x1000));
    unsupported[0]=0x14000001; assert(!cr_trampoline(out,unsupported,0x1000));
    puts("chams relocation: captured crashing BL, negative offsets, resume address and rejection checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c'; binary=Path(d)/'test'; c.write_text(source)
    subprocess.run(['cc','-std=c11','-Werror','-I',str(root),str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
