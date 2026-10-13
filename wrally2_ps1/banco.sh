#!/bin/sh
# Banco de pruebas del World Rally 2 en el PC: prepara los datos de tus ROM (WR2_ROM = carpeta o zip de MAME, por
# defecto ./rom), descubre el codigo del 68000, lo recompila a C y compila rc/rc_host. Con MUSASHI (ruta de Musashi,
# https://github.com/kstenerud/Musashi) compila tambien rc_host_dbg, que compara instruccion a instruccion.
# Uso: ./banco.sh            (despues: cd rc && ./rc_host 12000 ../work/run, ver la cabecera de rc/rc_host.c)
set -e
cd "$(dirname "$0")"
tools/guardia_wr1.sh
python3 tools/prep.py
python3 tools/descent.py
python3 tools/alcance15.py
python3 rc/rc68.py rc/wr2_rc.c
python3 rc/rc51c.py rc/mcu_rc.c
cd rc
CF="-O1 -w -DMCS51_XOR=1 -I."
gcc $CF -c wr2_rc.c -o wr2_rc.o
gcc $CF -o rc_host rc_host.c rc_mem.c gae1.c mcu.c mcu_rc.c wr2_rc.o
if [ -n "$MUSASHI" ]; then
  B=../work/musashi; mkdir -p $B
  if [ ! -f $B/m68kops.o ]; then
    cp "$MUSASHI"/*.c "$MUSASHI"/*.h $B/ && cp -r "$MUSASHI"/softfloat $B/
    sed -i 's/#define M68K_EMULATE_INT_ACK        M68K_OPT_OFF/#define M68K_EMULATE_INT_ACK        M68K_OPT_ON/' $B/m68kconf.h
    printf '\nvoid m68k_check_irq_now(void) { m68ki_check_interrupts(); }\n' >> $B/m68kcpu.c
    (cd $B && gcc -O2 -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c >/dev/null)
    for f in m68kcpu m68kops m68kdasm; do gcc -O1 -w -I$B -c $B/$f.c -o $B/$f.o; done
    gcc -O1 -w -I$B -c $B/softfloat/softfloat.c -o $B/softfloat.o
  fi
  gcc $CF -DRCDBG -I$B -c wr2_rc.c -o $B/wr2_rc_dbg.o
  gcc $CF -DRCDBG -I$B -o rc_host_dbg rc_host.c rc_mem.c gae1.c mcu.c mcu_rc.c $B/wr2_rc_dbg.o \
      $B/m68kcpu.o $B/m68kops.o $B/m68kdasm.o $B/softfloat.o -lm
fi
echo "banco listo: rc/rc_host$( [ -n "$MUSASHI" ] && echo ' y rc/rc_host_dbg')"
