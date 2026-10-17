# uso: runp.sh outdir ST "frames"
export PATH=$PATH:/usr/games
cd /home/claude/wrally/neo/mame; rm -rf /tmp/claude-0/s/sd_$1
S=/tmp/claude-0/s; GF="$3" ST=$2 FR="$3" timeout 900 mame aes -rompath realroms -hashpath hash_wrallyng -cart wrallyng -video none -sound none -nothrottle -snapshot_directory /tmp/claude-0/s/sd_$1 -snapname '%g/%i' -autoboot_script ${LUA:-/tmp/claude-0/s/play.lua} 2>&1 | grep -v "^$" | grep -iv "redump\|might not" | tail -${TL:-3}
rm -rf $S/$1; mkdir -p $S/$1; mv /tmp/claude-0/s/sd_$1/*/*.png $S/$1/
python3 /tmp/claude-0/s/grid.py $S/$1 $S/$1.png ${COLS:-5}
