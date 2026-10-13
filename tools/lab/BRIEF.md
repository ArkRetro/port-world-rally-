# World Rally Neo Geo port — shared lab brief (read fully before working)

## Problem
A homebrew port of Gaelco's arcade **World Rally** (1993) to Neo Geo (`WRally_neogeo_final_v6.neo`, and a later
`fix` build) **resets back to the BIOS on real hardware** (AES console with original BIOS and with UniBIOS 4.0,
game loaded through a **NeoSD Pro** flash cart). In MAME it never resets.

Real-hardware video (1 fps + 5 fps analysis): power-on → BIOS eye-catcher (~10 s) → the port's own blue
"Version 1.0 / RAM OK / ROM Checksum 0E56 / Coprocessor OK" screen (~8 s, it is the arcade's self-test, ported)
→ game intro: Gaelco logo → car-jump photo → pilot with helmet → "WORLD RALLY Championship" letters fully drawn
(~37.4 s) → **≤0.2 s later the screen goes black and the BIOS restarts**. No freeze is visible before it.
Happens both in demo (no input) and when inserting coins. A variant with an empty Z80 M1 ROM (no sound) resets
identically. Another commercial game with a 2 MB P-ROM (Kizuna Encounter) and a small port (RoboCop) work fine
on the same hardware.

## Key facts already established (do not re-derive)
* MAME 0.264 is installed: `/usr/games/mame`. The **neogeo watchdog in MAME = 0.1352 s (8 frames)**, measured.
  (`aes` driver: no watchdog reset observed.) Real hardware watchdog is believed to be the same ~0.13 s.
* Max gap between watchdog kicks (byte write to 0x300001) for the WR port in MAME over 0..45 s incl. the title:
  **38 ms** (at t≈12 s), far from 135 ms. So in MAME the port never comes close.
* Intro of the port is a frame-counter script: counter word **$10F38C**. Script code at 0x00D9BA..0x00E2A6.
  Phases fire on exact counts (0x01,0x44,0x64,0x86,0xFA,0x190,0x19A,0x276,0x27C,0x282,0x288,0x28E,0x299,0x2DF,
  0x301,0x32F,0x351,0x36C,0x393,0x3B5,**0x3B6** ...) and the intro ends at 0x514 (cmpi at 0xDB16) → 0xE29A.
  From cnt>0x3B5 the code at **0xE16C** runs (logo shine/bounce: uses $10F3A4/$10F3A3, `jsr $20856A`,
  table $EEBFC/$EEC50 selected by `tst.w $10EC46`, copy loop from pointer $1024D0 into list at ($10F4F6)).
  Video timing puts the real-HW reset at cnt ≈ 0x3B0–0x3D0, i.e. right at this transition.
  First-time-executed code after cnt 0x3B6 (vs a trace of cnt 0x330–0x3A0): 0xE16C–0xE260, 0x1413E–0x143AE,
  0x2000E0–0x200106 (vector 10 / line-A handler: indirect call via table at 0x208876),
  0x200C36–0x200C50, 0x204E5C, 0x205F22–0x205FA0 (Z80 sound command queue sender, non-blocking, single poll per
  call), 0x2061C2, 0x20856A–0x208590, 0x2087C2–0x2087F8.
* The port contains an emulation/translation layer: pointer translation via 0x2000BA (stack switch to $109000)
  and 0x203102, ROM "paging" at 0x200572 (page table 0x26101C, data 0x26121C+), line-A trap dispatch, arcade
  input/DSW shadows at $10A000.. (DSW default 0xFFDF written at 0x200F7E/0x200FC8, read at 0x240C/0x243A),
  arcade self-test RAM checks (0x20054A writes $AAAA/$5555).
* Port vector table: SSP=0x108C80, reset PC=0x2400, bus/addr/illegal → game handlers 0x536..0x582,
  **VBlank (vector 25)=0x200C76, Timer (26)=0x200098, Level3 (27)=0x20008E, line-A (10)=0x2000E0** — i.e. in the
  *banked* P2 window. Kizuna: SSP=0x10F300, reset=0xC00402, bus/addr/illegal → BIOS 0xC00408/0C0040E/0xC00414,
  VBlank=0x1102, Timer=0x11F8 (fixed P1 area).
* VBlank handler 0x200C76: `cmpi.w #$5752,$10BFFE; beq game; jmp $C00438 (BIOS SYSTEM_INT1)`; game path acks
  IRQ (`move.w #4,$3C000C`), kicks watchdog, calls code through 0x2000BA.
* Only ROM-space write by the port in 48 s: word 0 → 0x2FFFF0 at pc 0x226 (USER entry: kick wd, write bank reg,
  `jmp $20005A`). Kizuna writes nothing to ROM space.
* The port writes to 0x10F300–0x10FFFF (BIOS-reserved RAM), but beyond the initial clear only ~30 words
  (0x10F38C, 0x10F3A3/4, 0x10F4EE–0x10F51A, 0x10F826 ...). BIOS was not observed touching those afterwards.
  Filling game RAM with FF/00/AA/55/A5 at USER entry changed nothing in MAME.
* No 68k exceptions, no unmapped accesses, no data reads ≥ 0x2C0000 observed in MAME.

## Lab layout (everything under LAB=/tmp/claude-0/-home-user-port-world-rally-/307720d2-25b6-5a9f-8356-703187f15fbf/scratchpad/lab)
* `roms/` : `neogeo.zip` (BIOS set incl. UniBIOS 1.0–4.0, sp-s2 = Europe MVS v2 default), `aes.zip`,
  softlist item dirs `kiz/` (Kizuna Encounter from .neo, P 2 MB flat), `wrv6/` (port v6), `wrfix/` (port fix).
* `hash/neogeo.xml` : custom softlist with items **kiz, wrv6, wrfix** (names must be lowercase).
* `arcade/` : `wrally.zip` = original Gaelco arcade set (MAME machine `wrally`, runs, "best available").
* P-ROM files `roms/*/X-p1.p1` are **word-swapped** (byte pairs swapped). Upper MB of the file maps to CPU 0x200000.
  CPU address A ≥ 0x200000 ⇒ file offset A-0x100000.
* `wdgaps.py debug.log` prints largest gaps between `WDK <totalcycles>` lines (68k @ 12 MHz).

## How to run (proven recipes)
Always work in **your own directory** `LAB/work/<your-label>/` (cd into it inside the same Bash command), because
`-debuglog` writes `debug.log` into the CWD. Use absolute -rompath/-hashpath.
```
cd $LAB/work/<me> && QT_QPA_PLATFORM=offscreen timeout 400 /usr/games/mame neogeo wrv6 \
  -rompath $LAB/roms -hashpath $LAB/hash -video none -sound none -nothrottle \
  -debug -debuglog -debugscript my.cmd -autoboot_script my.lua -seconds_to_run 48 \
  -cfg_directory cfg -nvram_directory cfg
```
* Without `-debug`, drop QT_QPA_PLATFORM/-debuglog. Add `-bios unibios40` (or euro, japan, us ...) to change BIOS.
* Screenshots: `SDL_VIDEODRIVER=dummy ... -video soft -window -snapshot_directory snaps` and call
  `manager.machine.video:snapshot()` from Lua.
* Debugger script examples (output goes to debug.log thanks to -debuglog):
  `wpset 300001,1,w,1,{ printf "WDK %d\n",totalcycles ; g }` ;
  `wpset 3c0000,10,w,1,{ printf "LSPC %x %x pc=%x cnt=%x\n",wpaddr,wpdata,curpc,w@10f38c ; g }` ;
  `bpset 205f22,1,{ printf "X d0=%x\n",d0 ; g }` ; condition example `curpc<c00000`.
  Symbols: `curpc`, `wpaddr`, `wpdata`, `totalcycles`, registers `d0..a7`, memory `w@addr`, `d@addr`, `b@addr`.
  Note: printf that reads memory with w@ triggers read watchpoints on that address — avoid self-induced hits.
  `dasm file.txt,<start>,<len>,1` then `go` disassembles to a file. `trace file.log,0,noloop` / `trace off`
  (trace is ~35 MB per emulated second, keep windows short).
* Lua: `local cpu=manager.machine.devices[":maincpu"]; local mem=cpu.spaces["program"]`;
  `cpu.state["CURPC"].value` (read). Frame hook: `emu.register_frame_done(fn)`. From Lua you can drive the
  debugger: `manager.machine.debugger:command("trace x.log,0,noloop")` (needs -debug).
  Memory taps: `TAP = mem:install_write_tap(a,b,"name",function(off,data,mask) ... return data end)` — **must be
  stored in a global variable** or they get garbage-collected (unstored taps also caused segfaults earlier).
  Live ROM patch: `mem:write_direct_u16(addr,val)`. Inputs: `manager.machine.ioport.ports[":AUDIO_COIN"].fields["Coin 1"]`,
  `[":edge:joy:START"].fields["1 Player Start"]`, `[":edge:joy:JOY1"].fields["P1 A"]` with `:set_value(0/1)`.
* Frame timeline of wrv6 in MAME (no input): game code starts ~frame 700, port self-test (blue) then intro;
  intro counter reaches 0x3B6 at about frame 2290–2300; 0x479 at frame 2700.
* Kizuna (`kiz`) boots normally in MAME; use it as the commercial reference.
* Disk: keep traces short and delete big logs when done. 4 CPUs total and 2 agents run at once.
