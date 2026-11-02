#!/usr/bin/env python3
"""Replay an opt-in decoder capture without accessing the device or network."""
import argparse,json,struct,sys,time
from pathlib import Path
try:
 import unicorn as u
except ImportError:
 sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'evidence/local/replay-tools'))
 import unicorn as u
from unicorn import arm64_const as a

def replay(prefix, trace=None, tick_mode="recorded", coherent=False, syscall_clrex=False, seek_end=None, counter_frequency=None, gpu_address=None, quiet=False, normalize_pc=False, ctr=None):
 p=Path(prefix);meta=json.loads(p.with_suffix('.json').read_text());raw=p.with_suffix('.bin').read_bytes()
 if counter_frequency is None:counter_frequency=meta.get('counterFrequency')
 uc=u.Uc(u.UC_ARCH_ARM64,u.UC_MODE_ARM)
 # Match the on-device register numbering, including its special-register handling.
 regs=[a.UC_ARM64_REG_X0+i for i in range(29)]+[a.UC_ARM64_REG_FP,a.UC_ARM64_REG_LR]
 off=0;pages={}
 while off<len(raw):
  addr,prot,size=struct.unpack_from('<QII',raw,off);off+=16
  if size!=4096 or off+size>len(raw):raise ValueError('Invalid page')
  data=raw[off:off+size];off+=size;pages[addr]=data
  uc.mem_map(addr,size,u.UC_PROT_ALL);uc.mem_write(addr,data)
 if normalize_pc:
  for addr,data in list(pages.items()):
   canonical=addr&0x00ffffffffffffff
   if canonical not in pages:
    uc.mem_map(canonical,4096,u.UC_PROT_ALL);uc.mem_write(canonical,data);pages[canonical]=data
 for reg,val in zip(regs,meta['x']):uc.reg_write(reg,int(val,16))
 for i,val in enumerate(meta['q']):uc.reg_write(a.UC_ARM64_REG_Q0+i,int(val,16))
 for reg,key in [(a.UC_ARM64_REG_SP,'sp'),(a.UC_ARM64_REG_TPIDR_EL0,'tls')]:uc.reg_write(reg,int(meta[key],16))
 uc.reg_write(a.UC_ARM64_REG_NZCV,meta['nzcv'])
 ticks=iter(meta['ticks']); trace_file=open(trace,'w') if trace else None
 if trace_file:
  import capstone
  dis=capstone.Cs(capstone.CS_ARCH_ARM64,capstone.CS_MODE_ARM)
 exclusive_cleared=False
 counts={'instructions':0,'ticks':0,'svc':[],'interrupts':[]};failure=[]
 def code(uc,pc,size,_):
  nonlocal exclusive_cleared
  if normalize_pc and pc>>56:
   uc.reg_write(a.UC_ARM64_REG_PC,pc&0x00ffffffffffffff);return
  counts['instructions']+=1
  insn=struct.unpack('<I',uc.mem_read(pc,4))[0]
  if trace_file:
   op=next(dis.disasm(bytes(uc.mem_read(pc,4)),pc),None)
   trace_file.write(f'{pc:x} {op.mnemonic if op else "?"} {op.op_str if op else "?"} '+ ' '.join(f'x{i}={uc.reg_read(regs[i]):x}' for i in range(31))+f' sp={uc.reg_read(a.UC_ARM64_REG_SP):x}\n')
  if gpu_address is not None and pc==0x78d2acc7ec and uc.reg_read(a.UC_ARM64_REG_X1)==0xc0300947:
   uc.mem_write(uc.reg_read(a.UC_ARM64_REG_X2),struct.pack('<Q',gpu_address))
  if (insn&0xffffffe0)==0xd53be040:
   try:tpc,val=next(ticks)
   except StopIteration:failure.append('Unexpected counter read');uc.emu_stop();return
   if int(tpc,16)!=pc:failure.append(f'Counter PC mismatch: {pc:x} != {tpc}');uc.emu_stop();return
   counts['ticks']+=1
   if tick_mode=='frozen':val=meta['ticks'][0][1]
   elif tick_mode=='fast':val=meta['ticks'][0][1]+counts['ticks']
   uc.reg_write(a.UC_ARM64_REG_X0+(insn&31),val);uc.reg_write(a.UC_ARM64_REG_PC,pc+4)
  elif (insn&0xffffffe0)==0xd53bd040:
   uc.reg_write(a.UC_ARM64_REG_X0+(insn&31),int(meta['tls'],16));uc.reg_write(a.UC_ARM64_REG_PC,pc+4)
  elif ctr is not None and (insn&0xffffffe0)==0xd53b0020:
   uc.reg_write(a.UC_ARM64_REG_X0+(insn&31),ctr);uc.reg_write(a.UC_ARM64_REG_PC,pc+4)
  elif counter_frequency is not None and (insn&0xffffffe0)==0xd53be000:
   uc.reg_write(a.UC_ARM64_REG_X0+(insn&31),counter_frequency);uc.reg_write(a.UC_ARM64_REG_PC,pc+4)
  elif syscall_clrex and (insn&0x3fe0fc00)==0x08007c00 and exclusive_cleared:
   rs=(insn>>16)&31;uc.reg_write(a.UC_ARM64_REG_X0+rs,1);uc.reg_write(a.UC_ARM64_REG_PC,pc+4);exclusive_cleared=False
  elif (insn&0xffe0001f)==0xd4000001:
   exclusive_cleared=True
   nr=uc.reg_read(a.UC_ARM64_REG_X8);counts['svc'].append([hex(pc),nr])
   value={172:meta['pid'],178:meta['tid']}.get(nr,0)
   if nr==62 and seek_end is not None:value=seek_end
   uc.reg_write(a.UC_ARM64_REG_X0,value);uc.reg_write(a.UC_ARM64_REG_PC,pc+4)
 def intr(uc,no,_):
  pc=uc.reg_read(a.UC_ARM64_REG_PC);insn=struct.unpack('<I',uc.mem_read(pc,4))[0]
  counts['interrupts'].append([hex(pc),no,hex(insn)])
  if pc in [0x7100000000,0x7100000004]:uc.emu_stop();return
  if no==2 and (insn&0x3fe0fc00)==0x08007c00:
   rn=(insn>>5)&31;rt=insn&31;rs=(insn>>16)&31;n=1<<(insn>>30)
   addr=uc.reg_read(a.UC_ARM64_REG_X0+rn);val=uc.reg_read(a.UC_ARM64_REG_X0+rt)
   uc.mem_write(addr,val.to_bytes(8,'little')[:n]);uc.reg_write(a.UC_ARM64_REG_X0+rs,0);uc.reg_write(a.UC_ARM64_REG_PC,pc+4);return
  failure.append(f'Unhandled interrupt {no} at {pc:x}');uc.emu_stop()
 def missing(uc,access,addr,size,value,_):
  failure.append(f'Missing memory access={access} addr={addr:x} size={size} pc={uc.reg_read(a.UC_ARM64_REG_PC):x}');return False
 uc.hook_add(u.UC_HOOK_CODE,code);uc.hook_add(u.UC_HOOK_INTR,intr);uc.hook_add(u.UC_HOOK_MEM_UNMAPPED,missing)
 start=time.monotonic()
 try:uc.emu_start(int(meta['pc'],16),0x7100000000,timeout=5000000,count=2000000)
 except u.UcError as e:failure.append(str(e))
 if trace_file:trace_file.close()
 result=struct.unpack('<12f',uc.mem_read(0x7200000300,48));pc=uc.reg_read(a.UC_ARM64_REG_PC)
 match=all(abs(x-y)<max(1e-5,abs(y)*1e-8) for x,y in zip(result,meta['result']))
 out={'file':str(p),'unicorn':u.__version__,'seconds':round(time.monotonic()-start,3),'pc':hex(pc),'matches':match,'result':result,'expected':meta['result'],'counts':counts,'errors':failure}
 if not quiet:print(json.dumps(out))
 return out
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('prefix',nargs='+');parser.add_argument('--trace');parser.add_argument('--ctr',type=lambda v:int(v,0));parser.add_argument('--normalize-pc',action='store_true');parser.add_argument('--gpu-address',type=lambda v:int(v,0));parser.add_argument('--counter-frequency',type=int);parser.add_argument('--seek-end',type=int);parser.add_argument('--syscall-clrex',action='store_true');parser.add_argument('--tick-mode',default='recorded',choices=['recorded','frozen','fast']);args=parser.parse_args()
 for prefix in args.prefix:replay(prefix,args.trace,args.tick_mode,syscall_clrex=args.syscall_clrex,seek_end=args.seek_end,counter_frequency=args.counter_frequency,gpu_address=args.gpu_address,normalize_pc=args.normalize_pc,ctr=args.ctr)
