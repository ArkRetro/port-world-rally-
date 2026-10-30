# prof.sh T -> traza 4 cuadros en carrera y resume
cd /home/claude/wrally/neo/mame; export PATH=$PATH:/usr/games
rm -rf /tmp/claude-0/s/sd_tr /tmp/claude-0/s/tr_race.txt
NF=${NF:-3} G=${1:-6600} timeout 900 mame aes -rompath realroms -hashpath hash_wrallyng -cart wrallyng -video none -sound none -nothrottle -debug -debugger none -snapshot_directory /tmp/claude-0/s/sd_tr -autoboot_script /tmp/claude-0/s/trc5.lua > /dev/null 2>&1
cd /home/claude/wrally/neo/build; python3 - <<'PY'
import pickle,bisect,collections
s=pickle.load(open('syms.pkl','rb'))
it=s.items() if isinstance(s,dict) else s
c=sorted((v,k) for k,v in it if isinstance(v,int) and v>=0x200000)
C=collections.Counter(); tot=0; fr=0; idle=0
for l in open('/tmp/claude-0/s/tr_race.txt'):
    t=l.split(':')[0].strip()
    try: a=int(t,16)
    except: continue
    tot+=1
    if a==0x32c0: fr+=1
    if a in (0x32b8,0x32be): idle+=1
    if a<0x200000: C['game']+=1; continue
    i=bisect.bisect_right(c,(a,'~'))-1; C[c[i][1]]+=1
print('total',tot,'game frames',fr,'idle',idle)
for k,v in C.most_common(14): print(v,k)
PY
