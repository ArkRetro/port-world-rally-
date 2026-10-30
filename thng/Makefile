# Thunder Hoop -> Neo Geo port
#
#   make game ARCADE=<dir with the thoop set>  -> build/thng-*.{p1,s1,m1,v1,c1,c2} + MAME softlist build/hash
#
# Requires: m68k-linux-gnu-as/ld/objcopy (binutils), sdasz80/sdldz80/makebin (SDCC), python3 (+capstone, PIL)

AS      = m68k-linux-gnu-as
LD      = m68k-linux-gnu-ld
OBJCOPY = m68k-linux-gnu-objcopy
ASFLAGS = --register-prefix-optional -m68000 -I src
ARCADE ?= ../lab/thoop
BUILD  ?= build
VERSION := $(shell cat VERSION)

SRCS = src/header.s src/main.s src/crypt.s src/video.s src/fix.s src/sound.s src/tables.s
ANALYSIS = analysis/absrefs.csv analysis/trap_tables.csv analysis/code_ranges.csv analysis/vram_sites.txt

.PHONY: game neo kit clean

$(BUILD)/th_cpu.bin: $(ARCADE)/th18dea1.040 $(ARCADE)/th161eb4.020
	mkdir -p $(BUILD)
	python3 -I -c "import sys; e=open(sys.argv[1],'rb').read(); o=open(sys.argv[2],'rb').read(); b=bytearray(0x100000); b[0:2*len(e):2]=e; b[1:2*len(o):2]=o; open(sys.argv[3],'wb').write(b)" $^ $@

$(BUILD)/crypt.bin: tools/mkcrypt.py analysis/gaelcrypt.py
	mkdir -p $(BUILD)
	python3 -I tools/mkcrypt.py $@

$(BUILD)/font.s1: tools/mkfont.py
	mkdir -p $(BUILD)
	python3 -I tools/mkfont.py $@

$(BUILD)/thng-c1.c1 $(BUILD)/thng-c2.c2 $(BUILD)/map8.bin $(BUILD)/thng-s1.s1: tools/gfx2crom.py $(BUILD)/font.s1
	python3 -I tools/gfx2crom.py $(ARCADE)/c09 $(ARCADE)/c10 $(ARCADE)/c11 $(ARCADE)/c12 $(BUILD)/thng-c1.c1 $(BUILD)/thng-c2.c2 $(BUILD)/map8.bin --fix $(BUILD)/font.s1 $(BUILD)/thng-s1.s1

# sound: the OKI phrases converted to ADPCM-A (V ROM), their slots for the Z80 and their lengths for the 68k
$(BUILD)/thng-v1.v1 $(BUILD)/samples.inc $(BUILD)/sample_dur.inc $(BUILD)/okimap.csv: tools/oki2adpcma.py $(BUILD)/th_cpu.bin
	python3 -I tools/oki2adpcma.py $(ARCADE)/sound $(BUILD)/th_cpu.bin $(BUILD)/thng-v1.v1 $(BUILD)/samples.inc $(BUILD)/okimap.csv

$(BUILD)/thng-m1.m1: z80/driver.s $(BUILD)/samples.inc
	sdasz80 -I$(BUILD) -o $(BUILD)/driver.rel z80/driver.s
	sdldz80 -i -b CODE=0 $(BUILD)/driver $(BUILD)/driver.rel
	makebin -p -s 65536 $(BUILD)/driver.ihx $@
	python3 -I -c "import sys; d=open('$@','rb').read(); open('$@','wb').write(d+bytes(0x10000-len(d)))"

$(BUILD)/stubs.s: tools/mkrom.py analysis/vram_sites.txt $(BUILD)/th_cpu.bin
	python3 -I tools/mkrom.py stubs $(BUILD)/th_cpu.bin analysis/vram_sites.txt $@

$(BUILD)/game.elf: $(SRCS) $(BUILD)/stubs.s $(BUILD)/crypt.bin $(BUILD)/map8.bin $(BUILD)/sample_dur.inc src/neogeo.i src/ram.i src/seccode.bin game.ld
	for f in $(SRCS) $(BUILD)/stubs.s; do $(AS) $(ASFLAGS) -I $(BUILD) -o $(BUILD)/g_$$(basename $$f .s).o $$f || exit 1; done
	$(LD) -T game.ld -o $@ $(patsubst %,$(BUILD)/g_%.o,$(basename $(notdir $(SRCS)))) $(BUILD)/g_stubs.o

$(BUILD)/game.bin $(BUILD)/game.sym: $(BUILD)/game.elf
	$(OBJCOPY) -O binary --gap-fill=0 $< $(BUILD)/game.bin
	m68k-linux-gnu-nm $< > $(BUILD)/game.sym

$(BUILD)/thng-p1.p1: $(BUILD)/game.bin $(BUILD)/game.sym $(BUILD)/th_cpu.bin tools/mkrom.py $(ANALYSIS)
	python3 -I tools/mkrom.py build $(BUILD)/th_cpu.bin analysis $(BUILD)/game.bin $(BUILD)/game.sym $@

game: $(BUILD)/thng-p1.p1 $(BUILD)/thng-s1.s1 $(BUILD)/thng-m1.m1 $(BUILD)/thng-v1.v1 $(BUILD)/thng-c1.c1 $(BUILD)/thng-c2.c2
	python3 -I tools/mksoftlist.py $(BUILD) thng "Thunder Hoop NG v$(VERSION)" $(BUILD)/hash/neogeo.xml

clean:
	rm -rf $(BUILD)

# ---- release: NeoSD / MiSTer / Geolith .neo + zip of the MAME set ----
neo: game
	mkdir -p $(BUILD)/release
	python3 -I tools/mkneo.py $(BUILD)/roms/thng thng $(BUILD)/release/thng.neo -n "Thunder Hoop NG v$(VERSION)" --zip $(BUILD)/release/thng.zip
	cp $(BUILD)/release/thng.neo $(BUILD)/release/thng_v$(VERSION).neo

# ---- distributable kit: the port without any arcade data + the tool (command line / web page) that rebuilds
# thng.neo from the user's own Thunder Hoop ROMs ----
kit: neo
	python3 -I tools/mkkit.py $(BUILD) $(BUILD)/release/thng-kit.zip
	cp tools/thng_build.py $(BUILD)/release/
	python3 -I tools/mkweb.py $(BUILD)/release/thng-kit.zip tools/thng_build.py $(BUILD)/release/thng.html $(VERSION)
