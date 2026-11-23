/* Relocate the four overwritten AArch64 instructions. The original hook helper
 * copied PC-relative BL unchanged; vkCmdEndRenderPass calls inside those 16 bytes.
 * Support direct B/BL and ADR/ADRP; reject other PC-relative forms before touching code. */
#ifndef INTERNAL_CHAMS_RELOC_H
#define INTERNAL_CHAMS_RELOC_H
static unsigned cr_trampoline(uint32_t *out, const uint32_t *source, uintptr_t pc) {
    unsigned n=0;
    for(unsigned i=0;i<4;i++) {
        uint32_t ins=source[i];
        if((ins & 0x7c000000u)==0x14000000u) {
            int64_t delta=(int64_t)((int32_t)(ins<<6)>>6)*4;
            uintptr_t dest=pc+i*4+delta;
            if(dest>=pc && dest<pc+16) return 0;
            int link=!!(ins & 0x80000000u);
            if(n+(link?5:4)+4>16) return 0;
            out[n++]=link?0x58000070u:0x58000050u; /* LDR X16, literal */
            out[n++]=link?0xd63f0200u:0xd61f0200u; /* BLR/BR X16 */
            if(link) out[n++]=0x14000003u; /* skip address after return */
            memcpy(&out[n],&dest,8); n+=2;
        } else if((ins & 0x1f000000u)==0x10000000u) {
            if(n+4+4>16) return 0;
            uint32_t imm=((ins>>5)&0x7ffffu)<<2 | ((ins>>29)&3u);
            int64_t delta=(int64_t)((int32_t)(imm<<11)>>11);
            uintptr_t address=(ins&0x80000000u)?(pc+i*4)&~(uintptr_t)4095:pc+i*4;
            address+=(ins&0x80000000u)?delta*4096:delta;
            uint32_t reg=ins&31u;
            for(unsigned k=0;k<4;k++) out[n++]=(k?0xf2800000u:0xd2800000u)|(k<<21)|((uint32_t)((address>>(k*16))&65535u)<<5)|reg;
        } else {
            if((ins & 0x3b000000u)==0x18000000u || /* literal loads */
               (ins & 0xff000010u)==0x54000000u || /* B.cond */
               (ins & 0x7e000000u)==0x34000000u || /* CBZ/CBNZ */
               (ins & 0x7e000000u)==0x36000000u) return 0; /* TBZ/TBNZ */
            if(n+1+4>16) return 0;
            out[n++]=ins;
        }
    }
    uintptr_t resume=pc+16;
    out[n++]=0x58000050u; out[n++]=0xd61f0200u;
    memcpy(&out[n],&resume,8); n+=2;
    return n*4;
}
#endif
