# uso: run_stage.sh R S N
R=$1; S=$2; N=${3:-7000}; G0=5650; G1=$((G0+N)); T=/tmp/claude-0/s/st$R$S; mkdir -p $T
export PATH=$PATH:/usr/games
(cd /home/claude/wrally/mame && RAL=$R STG=$S INP=rs G0=$G0 G1=$G1 OUT=$T/o.gz timeout 3000 mame wrally -rompath roms -video none -sound none -nothrottle -autoboot_script /tmp/claude-0/s/cap2.lua >/dev/null 2>&1) &
(cd /home/claude/wrally/neo/mame && PORT=1 RAL=$R STG=$S INP=rs DS=$((0x10a708)) CV=$((0x10ac58)) G0=$G0 G1=$G1 OUT=$T/p.gz timeout 3000 mame aes -rompath realroms -hashpath hash_wrallyng -cart wrallyng -video none -sound none -nothrottle -autoboot_script /tmp/claude-0/s/cap2.lua >/dev/null 2>&1) &
wait
python3 /tmp/claude-0/s/stage_cmp.py $T/o.gz $T/p.gz $T > $T/log.txt 2>&1
tail -1 $T/log.txt
