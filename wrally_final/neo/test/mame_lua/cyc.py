import re,sys,pickle,bisect,collections
s=pickle.load(open('/home/claude/wrally/neo/build/syms.pkl','rb'))
it=s.items() if isinstance(s,dict) else s
c=sorted((v,k) for k,v in it if isinstance(v,int) and v>=0x200000)
lines=[l for l in open(sys.argv[1]) if re.match(r'^[0-9A-F]{6}:',l)]
def nregs(r):
    n=0
    for part in r.split('/'):
        m=re.match(r'([DA])(\d)-([DA])?(\d)',part)
        if m: n+=int(m.group(4))-int(m.group(2))+1
        elif re.match(r'[DA]\d',part): n+=1
    return n
C=collections.Counter(); tot=0
for i,l in enumerate(lines):
    a=int(l[:6],16); ins=l[8:].strip()
    mn=ins.split()[0]; ops=ins[len(mn):].strip()
    sz='.l' in mn
    L=1
    if i+1<len(lines):
        d=int(lines[i+1][:6],16)-a
        if 2<=d<=10: L=d//2
        else: L=1+ops.count('$')+ ops.count('#')
    cy=4*L
    # operandos de memoria
    opl=[o.strip() for o in re.split(r',(?![^(]*\))',ops)] if ops else []
    base=mn.split('.')[0]
    if base=='movem':
        cy+=nregs(opl[0] if 'D' in opl[0] or 'A' in opl[0][:1] else opl[1])*(8 if sz else 4)+4
    else:
        for j,o in enumerate(opl):
            if o.startswith('#') or re.fullmatch(r'[DA]\d|SR|CCR|USP',o) or base in('lea','pea','jmp','jsr','bra','bsr','dbra','dbf','dbne') or base.startswith('b'): continue
            mem=8 if sz else 4
            if '-(' in o: cy+=2
            if j==len(opl)-1 and base not in('move','movea','clr','st','sf','seq','sne','scc','scs','smi','tst','cmp','cmpi','cmpa','cmpm','btst','moveq'): cy+=2*mem
            else: cy+=mem
    if base in('mulu','muls'): cy+=66
    elif base in('divu','divs'): cy+=136
    elif base in('lsl','lsr','asl','asr','rol','ror','roxl','roxr'):
        m=re.match(r'#(\d+)',ops); cy+=2+2*(int(m.group(1)) if m else 8)
    elif base in('jsr','bsr'): cy+=10
    elif base=='rts' or base=='rte': cy+=12
    elif base.startswith('b') and base not in('btst','bset','bclr','bchg'): cy+=6
    elif base.startswith('db'): cy+=6
    elif base in('swap','ext','exg') : cy+=2
    tot+=cy
    if a<0x200000: C['game']+=cy; continue
    k=bisect.bisect_right(c,(a,'~'))-1; C[c[k][1]]+=cy
print('total cycles',tot,'per vblank',tot//int(__import__('os').environ.get('NF','3')))
for k,v in C.most_common(22): print('%7d %5.1f%% %s'%(v,100*v/tot,k))
