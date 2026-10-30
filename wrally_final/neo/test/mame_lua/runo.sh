export PATH=$PATH:/usr/games
cd /home/claude/wrally/mame; rm -rf /tmp/claude-0/s/sd_$1
S=/tmp/claude-0/s; ST=$2 FR="$3" timeout 900 mame wrally -rompath roms -video none -sound none -nothrottle -snapshot_directory /tmp/claude-0/s/sd_$1 -snapname '%g/%i' -autoboot_script ${LUA:-/tmp/claude-0/s/play.lua} 2>&1 | tail -1
rm -rf $S/$1; mkdir -p $S/$1; mv /tmp/claude-0/s/sd_$1/*/*.png $S/$1/
python3 /tmp/claude-0/s/grid.py $S/$1 $S/$1.png ${COLS:-5}
