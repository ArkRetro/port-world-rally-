import capstone
md=capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN|capstone.CS_MODE_M68K_000)
d=open('/home/claude/wrally/prog.bin','rb').read()
for a in range(0x400,0x20000,2):
  try: i=next(md.disasm(d[a:a+10],a))
  except StopIteration: continue
  if i.mnemonic in ('lea.l','movea.l','move.l') and (i.op_str.startswith('$') or i.op_str.startswith('#$')):
    try: t=int(i.op_str.split(',')[0].lstrip('#$').split('.')[0],16)
    except ValueError: continue
    if 0x400<=t<0x20000:
      nxt=list(md.disasm(d[a+i.size:a+i.size+24],a+i.size))[:4]
      s=' ; '.join(j.mnemonic+' '+j.op_str for j in nxt)
      if ')+' in s and (',a' in i.op_str or ', a' in i.op_str): print(hex(a),i.mnemonic,i.op_str,'|',s)
