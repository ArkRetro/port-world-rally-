import capstone,re
md=capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN|capstone.CS_MODE_M68K_000)
d=open('/home/claude/wrally/prog.bin','rb').read()
res=set()
for a in range(0x400,0x20000,2):
  try: i=next(md.disasm(d[a:a+10],a))
  except StopIteration: continue
  if i.mnemonic.startswith(('cmp','sub','eor')) and '.l' in i.mnemonic or (i.mnemonic=='move.l' and re.match(r'\$e[0-9a-f]{4}\.l, d',i.op_str)):
    m=re.search(r'\$(e[0-9a-f]{4}|f[0-9a-f]{4})\.l',i.op_str)
    if m: res.add((hex(a),i.mnemonic,i.op_str))
for r in sorted(res): print(*r)
