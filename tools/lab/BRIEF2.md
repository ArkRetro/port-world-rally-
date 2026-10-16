# Brief 2 — HANDOVER build vs. games that work on NeoSD Pro AES (read this AND BRIEF.md in this folder)

LAB=/tmp/claude-0/-home-user-port-world-rally-/307720d2-25b6-5a9f-8356-703187f15fbf/scratchpad/lab

## What the user asked (Spanish, verbatim)
"Revisa ese archivo [WRally_NeoSD_PRO_AES_HANDOVER_v6.neo], compara con código y ejecución de juegos originales y
ports de Teo Tormo, hay que igualar las rutinas de lectura" — review the HANDOVER build, compare its code AND its
execution with original commercial games and with Teo Tormo's ports, and make the World Rally *read routines*
(how the program reads ROM: instruction fetch and data reads, P1 vs P2, banking) match theirs.
The user does NOT want more diagnostic test ROMs; the end goal is a playable build that loads/reads like the games that work.

## Established on the user's real hardware (AES + NeoSD Pro, original BIOS and UniBIOS 4.0)
* World Rally port resets to BIOS ~37 s after power-on (end of intro title), in demo and with coins. Never in MAME.
* Exception-catcher builds photographed on the CRT: VEC 04 illegal instruction / VEC 03 address error, ALWAYS with PC in
  P2 code (0x205308, 0x204248, 0x204108, 0x200DAC), different scenes each time. Memory dump around PC read back as
  `7204 7204 ...` (= the catcher's own next prefetched opcode) and a re-read loop read its own opcodes `51C8`/`B25A`:
  that is the signature of OPEN BUS — the NeoSD intermittently stops driving data for the P2/PORT window
  (0x200000-0x2FFFFF) for ~0.1-1 s, then recovers (P2 checksum correct ~1 s later). P1 code (0x000000-0x0FFFFF) kept
  running fine during the outage (the catcher lives in P1).
* Games that WORK on the same console + NeoSD: Kizuna Encounter (commercial, 2 MB P: code in P1, P2 = data only, no bank
  writes), World Heroes (commercial, 1 MB P), RoboCop "MVS conversion by TEO TORMO" (credits: "PLANNING AND DIRECTION
  TEO TORMO / CODING CLAUDE BY ANTHROPIC"), 1 MB P only.
* Software cannot add wait states; NOPs don't lengthen bus cycles. Do not propose those.

## The files (CPU-order binaries, already byte-swapped; offset = CPU address for 0x000000-0x0FFFFF,
## offset = CPU address - 0x100000 for 0x200000-0x2FFFFF)
| file in LAB | what | P size |
|---|---|---|
| ho_p.bin  | World Rally HANDOVER v6 (the new build to review) | 2 MB |
| v6_p.bin  | World Rally final v6 (original port) | 2 MB |
| fix_p.bin | World Rally "fix" build | 2 MB |
| robo_p.bin| RoboCop by Teo Tormo (works) | 1 MB |
| kiz_p.bin | Kizuna Encounter (works) | 2 MB |
| wh3_p.bin | World Heroes set 3 (works) | 1 MB |
Disassembler: `python3 -I $LAB/dis.py <bin> <hexstart> <hexlen>` (capstone m68k, already installed).
Arcade original (MAME machine `wrally`, Gaelco 1993) is in `$LAB/arcade/wrally.zip` (run with `-rompath $LAB/arcade`).

## MAME softlist items (hash `$LAB/hash`, roms `$LAB/roms`), names lowercase:
`wrho` (HANDOVER), `wrv6`, `wrfix`, `robo` (RoboCop Teo Tormo), `kiz` (Kizuna), `wh3` (World Heroes).
All boot (verified): wrho reaches the WR title, robo its attract, wh3 its attract. Recipes are in BRIEF.md
(`/usr/games/mame neogeo <item> -rompath $LAB/roms -hashpath $LAB/hash ...`, add `-bios unibios40` for UniBIOS 4.0;
console driver: `/usr/games/mame aes <item> ...` — aes has no watchdog). Work ONLY inside your own dir
`$LAB/work/<your-label>/` (cd there in the same Bash command). Only 4 CPUs: keep runs short (≤ 120 emulated s unless
needed), use `-nothrottle -video none -sound none` when you don't need pictures, delete big logs when done.
To tell opcode fetches from data reads with Lua read taps, compare the tap address with CURPC (prefetch is within
~0..+10 bytes of CURPC) or use a short debugger `trace` window; say which method you used. Verify that taps on ROM
regions actually fire (install them after a few frames, keep them in globals — see BRIEF.md).

## What we already know about HANDOVER (ho_p.bin) — static diff vs v6
* It is a SOURCE-LEVEL REBUILD, not a hex patch: the native layer 0x200000-0x20C000 was re-assembled with logic changes
  (net 0x118 bytes shorter); everything after it in P2 (data, paging table, page data) is shifted down by 0x118
  (e.g. v6 0x220000 == HO 0x21FEE8; v6 page table 0x26101C -> HO ~0x260F04); ~1300 address words relocated, incl. P1
  references into P2. S/M/V/C ROMs identical to v6. .neo NGH 0x0999, name "WRally neogeo final", year 2026.
* Vectors: SSP 0x10FF80, reset 0x2400, exceptions 2,3,4 -> P1 stubs 0x392/0x39C/0x3A6, others 5-9,11,24,28-31 -> 0x3B0;
  all store vector number in $10BF10 and the frame in $10BF12.. then `jmp $236`: a P1 on-screen error screen
  ("WR v6 ERR", hex of PC/SR) that loops kicking the watchdog. Line-A 0x20011E, VBlank 0x200CC0, Timer 0x2000D6,
  Level3 0x2000CC -> STILL in P2.
* Header jump table fixed (0x122 `jmp $220`, 0x128/0x12E/0x134 `jmp $234` = rts). USER entry 0x220: kick watchdog,
  `move.w #0,$2FFFF0` (bank register write STILL present), `jmp $200058`.
* New boot at 0x200058: if $10FDAE<2 -> BIOS 0xC00444; else SR=2700, SSP=0x10FF80, $10FD80=0x80, write 0x3A0013
  (REG_SWPROM), Z80 command 1, wait, write 0x3A001B (REG_CRTFIX), Z80 command 3, wait x2, init 0x200E56, $10BFFE='WR',
  jmp 0x2400. Private interrupt stack moved 0x108C80-0x109000 -> 0x108AC0-0x108F00 (switcher 0x2000F8).
* Other changed native routines (v6 address -> HO address): 0x200B28/0x200B34, 0x200D7C, 0x200F10-0x200FE8 (DSW shadow
  init), 0x2010C0-0x201284, 0x201718-0x201C96, 0x201FF2, 0x202868-0x202A20, 0x20348A-0x203DFA, 0x20440C-0x204706,
  0x20498E-0x204A80, 0x204CC4, 0x205154, 0x205F10-0x20621C (Z80 sound command queue), 0x2088E4-0x20896A.
  A word-level alignment script is easy: difflib.SequenceMatcher on 2-byte words of v6_p.bin[0x100000:0x130000] vs
  ho_p.bin[...] (single-word replacements are relocations).
* In MAME (neogeo, default BIOS) HANDOVER boots, runs the self-test and reaches the title like v6.

## Known v6 internals (addresses are v6; HANDOVER equivalents shift, find them)
P1 = arcade 68k ROM (1 MB, same addresses) with ~6300 bytes patched; native layer in P2 0x200000-0x20B600 (~46 KB span,
~30 KB actually executed in 49 regions in v6); line-A dispatch table 0x208876; stack switcher 0x2000BA; software Gaelco
VRAM decrypt 0x203102; ROM paging 0x200572 (page table 0x26101C, 147 pages, data 0x26121C+); VBlank 0x200C76 (checks
$10BFFE=='WR' else BIOS 0xC00438); 240 Hz LSPC timer 0x200098; P1 free space only ~11 KB (largest hole 5.7 KB at 0x097C);
Neo Geo work RAM is essentially full (arcade RAM remapped there; BIOS owns 0x10F300+).
