cd /home/claude/wrally/neo/mame && export PATH=$PATH:/usr/games; R="mame aes -rompath realroms -hashpath hash_wrallyng -cart wrallyng -video none -sound none -nothrottle"; S=/tmp/claude-0/s
nmv(){ m68k-linux-gnu-nm ../build/wr.elf | awk -v n=$1 '$3==n{print $1}'; }
(TC=$((0x$(nmv tcol))) C1=$((0x$(nmv c1mask))) L0M=$((0x$(nmv l0mask))) TE=$((0x$(nmv tile_empty_tab))) timeout 1500 $R -autoboot_script $S/chk11.lua 2>&1 | grep "total\|^L\|c1mask\|l0mask" | tail -6 > $S/r_chk.txt) &
(timeout 1500 $R -autoboot_script $S/blk.lua 2>&1 | grep "^5600" | python3 -c "
import re,sys;t=sys.stdin.read();v=[int(b) for a,b in re.findall(r'(\d+):(\d+)',t)][1:]; print(sum(v),len(v))" > $S/r_blk.txt) &
(timeout 1500 $R -autoboot_script $S/irqlat2.lua 2>&1 | grep "LAT\|masked\|LATE" | tail -8 > $S/r_irq.txt) &
(G0=5600 GE=7000 timeout 1500 $R -autoboot_script $S/hw6.lua 2>&1 | grep "VIS\|frames" | head -20 > $S/r_hw.txt) &
wait; for f in chk blk irq hw; do echo "== $f"; cut -c1-300 $S/r_$f.txt; done
