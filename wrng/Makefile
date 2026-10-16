# World Rally -> Neo Geo port: build the test viewer / the game ROM set
#
#   make viewer   -> build/viewer/wrng-p1.p1 (+ s1, m1, v1, c1, c2) and a MAME softlist in build/hash
#
# Requires: m68k-linux-gnu-as/ld/objcopy (binutils), sdasz80/sdldz80/makebin (SDCC), python3 (+PIL)
# Inputs (not in git): ARCADE = directory with the World Rally arcade set, DUMPS = arcade frame dumps

AS      = m68k-linux-gnu-as
LD      = m68k-linux-gnu-ld
OBJCOPY = m68k-linux-gnu-objcopy
ASFLAGS = --register-prefix-optional -m68000 -I src
ARCADE ?= ../lab/src/wrally
DUMPS  ?= src/dumps
BUILD  ?= build

RT_SRCS = src/header.s src/main.s src/video.s src/fix.s
GAME_SRCS = src/glue.s src/mcu.s src/decrypt.s
WRPROG_HI ?= $(ARCADE)/wrallyd/worldr17.c23
WRPROG_LO ?= $(ARCADE)/wrallyd/worldr16.c22
WRDALLAS ?= $(ARCADE)/wrdallas.bin

.PHONY: all viewer game clean z80 gfx

all: viewer

# ---- Z80 driver (M1 ROM) ----
$(BUILD)/wrng-m1.m1: z80/driver.s z80/samples.inc
	mkdir -p $(BUILD)
	sdasz80 -o $(BUILD)/driver.rel z80/driver.s
	sdldz80 -i -b CODE=0 $(BUILD)/driver $(BUILD)/driver.rel
	makebin -p -s 65536 $(BUILD)/driver.ihx $@
	python3 -I -c "import sys; d=open('$@','rb').read(); open('$@','wb').write(d+bytes(0x10000-len(d)))"

z80/samples.inc:
	echo "        .db 0,0,0,0" > $@

# ---- graphics (C ROMs) and fix (S ROM) ----
$(BUILD)/wrng-c1.c1 $(BUILD)/wrng-c2.c2: tools/gfx2crom.py
	mkdir -p $(BUILD)
	python3 -I tools/gfx2crom.py $(ARCADE)/worldr21.i13 $(ARCADE)/worldr20.i11 $(ARCADE)/worldr19.i09 $(ARCADE)/worldr18.i07 $(BUILD)/wrng-c1.c1 $(BUILD)/wrng-c2.c2

$(BUILD)/wrng-s1.s1: tools/mkfont.py
	mkdir -p $(BUILD)
	python3 -I tools/mkfont.py $@

$(BUILD)/wrng-v1.v1:
	mkdir -p $(BUILD)
	python3 -I -c "open('$@','wb').write(bytes(0x100000))"

# ---- viewer program (P1) ----
$(BUILD)/viewer.elf: $(RT_SRCS) src/viewer.s src/neogeo.i src/ram.i src/seccode.bin viewer.ld
	mkdir -p $(BUILD)
	for f in $(RT_SRCS) src/viewer.s; do $(AS) $(ASFLAGS) -I $(DUMPS)/.. -o $(BUILD)/$$(basename $$f .s).o $$f || exit 1; done
	$(LD) -T viewer.ld -o $@ $(BUILD)/header.o $(BUILD)/main.o $(BUILD)/video.o $(BUILD)/fix.o $(BUILD)/viewer.o

$(BUILD)/wrng-p1.p1: $(BUILD)/viewer.elf
	$(OBJCOPY) -O binary --pad-to=0x100000 --gap-fill=0xFF $< $@

viewer: $(BUILD)/wrng-p1.p1 $(BUILD)/wrng-s1.s1 $(BUILD)/wrng-m1.m1 $(BUILD)/wrng-v1.v1 $(BUILD)/wrng-c1.c1 $(BUILD)/wrng-c2.c2
	python3 -I tools/mksoftlist.py $(BUILD) wrng "World Rally NG test" $(BUILD)/hash/neogeo.xml

# ---- game build: arcade program + runtime -> 2 MB P ROM ----
$(BUILD)/wr_cpu.bin: $(WRPROG_HI) $(WRPROG_LO)
	mkdir -p $(BUILD)
	python3 -I -c "import sys; hi=open(sys.argv[1],'rb').read(); lo=open(sys.argv[2],'rb').read(); b=bytearray(len(hi)*2); b[0::2]=hi; b[1::2]=lo; open(sys.argv[3],'wb').write(b)" $(WRPROG_HI) $(WRPROG_LO) $@

$(BUILD)/stubs.s: tools/mkrom.py analysis/absrefs.csv $(BUILD)/wr_cpu.bin
	python3 -I tools/mkrom.py stubs $(BUILD)/wr_cpu.bin analysis/absrefs.csv $@

$(BUILD)/game.elf: $(RT_SRCS) $(GAME_SRCS) $(BUILD)/stubs.s src/neogeo.i src/ram.i src/seccode.bin game.ld
	for f in $(RT_SRCS) $(GAME_SRCS) $(BUILD)/stubs.s; do $(AS) $(ASFLAGS) -I $(dir $(WRDALLAS)) -o $(BUILD)/g_$$(basename $$f .s).o $$f || exit 1; done
	$(LD) -T game.ld -o $@ $(BUILD)/g_header.o $(BUILD)/g_main.o $(BUILD)/g_video.o $(BUILD)/g_fix.o $(BUILD)/g_glue.o $(BUILD)/g_mcu.o $(BUILD)/g_decrypt.o $(BUILD)/g_stubs.o

$(BUILD)/game.bin $(BUILD)/game.sym: $(BUILD)/game.elf
	$(OBJCOPY) -O binary --gap-fill=0 $< $(BUILD)/game.bin
	m68k-linux-gnu-nm $< > $(BUILD)/game.sym

$(BUILD)/wrng-p1.p1.game: $(BUILD)/game.bin $(BUILD)/game.sym $(BUILD)/wr_cpu.bin tools/mkrom.py
	python3 -I tools/mkrom.py build $(BUILD)/wr_cpu.bin analysis/absrefs.csv $(BUILD)/game.bin $(BUILD)/game.sym $@

game: $(BUILD)/wrng-p1.p1.game $(BUILD)/wrng-s1.s1 $(BUILD)/wrng-m1.m1 $(BUILD)/wrng-v1.v1 $(BUILD)/wrng-c1.c1 $(BUILD)/wrng-c2.c2
	cp $(BUILD)/wrng-p1.p1.game $(BUILD)/wrng-p1.p1
	python3 -I tools/mksoftlist.py $(BUILD) wrng "World Rally NG" $(BUILD)/hash/neogeo.xml

clean:
	rm -rf $(BUILD)
