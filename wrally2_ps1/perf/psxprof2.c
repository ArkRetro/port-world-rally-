/* psxprof2: copia de wrally_ps1/perf/psxprof.c (el WR1 no se toca) para el World Rally 2, con lo minimo cambiado:
   - HUELLA=fichero: al entrar en rc_vblank, huella FNV-1a de rc_ram (64 KB), rc_vram (64 KB), rc_pal y rc_xram (8 KB)
     una linea por llamada ("llamada hash"), la misma que escribe el banco (rc_host HUELLA): puerta G-ESTADO con
     tools/huella.py. La columna "estado" de cuadros.csv es esa misma huella.
   - INPUTS numera los cuadros como el banco y MAME (los vblanks pasados con la IRQ enmascarada cuentan, ~20 en el
     arranque: se leen rc_r.cyrel y rc_dl del EXE), no por llamadas a rc_vblank como SHOT y la huella.
   - INPUTS: la letra G (marcha, un nivel en el banco) pulsa el cuadrado un cuadro cada vez que cambia de nivel (en
     la PS1 la marcha es un conmutador por flanco del cuadrado, ps1/main.c); Q sigue siendo el cuadrado sin mas.
   - Sin la moneda y el START por defecto del WR1 (cuadros 600, 800...): solo con COIN1=1; el guion va en INPUTS.
   - WAV=fichero: lo que suena, mono a 44,1 kHz (recopilatorio/musica.sh saca de aqui la musica del titulo para el
     menu, sin emulador ni BIOS). Modelo sencillo de la SPU: ADPCM con banderas y repeticion, tono con interpolacion
     lineal, volumen de cada voz y general, envolvente de ataque inmediato y bajada lineal al KOFF.
   - SPU: direccion de repeticion, banderas de bloque, ENDX (ENDX_BORRA=1: se borra al escribirlo, como Mednafen; si no,
     como MiSTer) e IRQ de la SPU: para probar el encadenado de trozos (sucesos SPU_SALTO / SPU_FIN).
   - cpu.csv: por llamada a rc_vblank, ciclos de la CPU fuera de rc_vblank desde la llamada anterior (68000 + MCU
     por acceso + sucesos), ciclos de la llamada anterior a rc_vblank entera y de mcu_cuadro dentro, y el modo del
     juego (FEC298): puerta G-CPU con perf/cpu.py.
   Compilar: perf/compilar.sh. Lo demas es psxprof tal cual: */
/* psxprof: emulador PS1 minimo para perfilar el port de World Rally (EXE sin BIOS, todo por sondeo).
   Modelo de tiempos aproximado (constantes de DuckStation): CPU 33,8688 MHz, cache de instrucciones de 4 KB,
   lecturas de RAM de 6 ciclos, GPU con coste por pixel, CD con busqueda/lectura/pausa, mando SIO0.
   Uso: psxprof imagen.cue wr2.elf.bin(PS-EXE) wr2.nm cuadros_juego [salida_dir]
   Entradas (botones) segun el cuadro del juego (llamadas a rc_vblank), como rc_host (ver inputs_for).
   INPUTSV: como INPUTS pero por vblank (sigue contando con el juego parado, p. ej. en la pausa).
   SHOTV=v1,v2,...: capturas en esos vblanks (tambien antes del juego: la pantalla de carga del arranque);
   CLUTCACHE=1: la CLUT se guarda como en la GPU (una, se recarga al cambiar; GP0(01h) la vacia); 2: GP0(01h) no la vacia;
   MISSLOG=1: falta.txt con los tiles que faltan en cada cuadro; WATCH=var,...: cambios de esas variables
   (var+desp: la palabra desp bytes despues, p. ej. WATCH=cd_dbg+12 para cd_dbg[3]).
   CD_VIEJOS=n: tras un ReadN sin Pause llegan aun n sectores de la posicion anterior (psxprof no los da si no).
   Modelos del hardware real (consola con XStation: HC05 de verdad y lector emulado; PSIO: controlador en una FPGA),
   todos apagados por defecto (sin ellos psxprof hace exactamente lo mismo que antes):
   CD_STAT=1: stat de todas las respuestas segun lo que hace la unidad: bit6 (40h) buscando (SeekL/SeekP o la
     busqueda de ReadN/Play), bit5 (20h) leyendo, bit7 (80h) tocando (solo cuando acaba la busqueda de Play), bit1
     motor; la primera respuesta lleva el stat de cuando se manda el comando y la segunda (INT2 de Pause, Init,
     ReadTOC, SeekL/P) el de despues (solo motor). GetlocP durante una busqueda da la posicion de la cabeza
     (interpolada desde el origen, no el destino), y un Play/ReadN durante otra busqueda empieza desde ahi. Con
     CD_STAT o CD_PAUSA_REAL, SeekL/SeekP (15h/16h) buscan de verdad (INT3 y luego INT2 al acabar).
   CD_PAUSA_REAL=1: Pause durante la busqueda de Play o ReadN (antes del primer sector o del audio) o de SeekL/P se
     rechaza: primera respuesta INT5 {stat|1, 80h} y no se para nada (evento CD_PAUSE_RECHAZADA). Como DuckStation
     (probado en consola): la busqueda (y la lectura o el audio de despues) sigue; MiSTer en cambio cancela la
     lectura que venia tras la busqueda de ReadN (la de Play no).
   CD_SEEK_MUL=f: busquedas f veces mas largas; CD_SEEK_JIT=ms: a cada busqueda se le suma de 0 a ms ms al azar
     (latencia de la SD; generador fijo: el mismo en cada ejecucion, CD_SEED=n cambia la semilla).
     CD_SPEED_MS=ms: lo que tarda el cambio de velocidad x1<->x2 (por defecto 600 ms a doble y 1000 ms a normal).
   CD_INT3_RND=1: la primera respuesta (INT3 o INT5) tarda al azar entre el minimo y el maximo de psx-spx (4A73h a
     3115Bh ciclos, casi siempre cerca del minimo: media ~C000h, la de psx-spx C4E1h); Init y ReadTOC, de los que
     psx-spx solo da minimo y media, entre F820h y 1817Ch (media 13CCEh).
   CD_BUSY=1: BUSYSTS (bit7 de 1F801800h) a 1 desde que se escribe el comando hasta que se entrega su primera
     respuesta; un comando escrito mientras tanto se apunta (CD_CMD_OCUPADO) y se ejecuta igual.
   CD_GLITCH=N: HINTSTS inestable (psx-spx, 'Unstable IRQ Flag polling'): uno de cada N INT3 se ve 1 en la primera
     lectura de 1F801803h (banco 1) y uno de cada CD_GLITCH5 INT5 (por defecto N/10; se puede poner sola) se ve 4;
     la lectura siguiente ya da el valor bueno (evento CD_GLITCH). Solo puede pasar si la CPU esta sondeando cuando
     cambian los bits: cuentan los INT cuya primera lectura llega en los CD_GLITCH_VENT ciclos siguientes (2048 por
     defecto: los bucles de cd_poll leen a los 30-500 ciclos; las lecturas de despues del cuadro del 68000, a 2-15
     ms, ven ya los bits quietos, como el manejador de una IRQ); CD_GLITCH_VENT=0: cualquier primera lectura.
     psx-spx (0,1 % de los INT3 y 1 % de los INT5) lo midio sondeando en un bucle corto: con el de cd_poll, mas
     lento, N=1000 y N5=100 ya son pesimistas.
   CD_CORRUPTO=n: uno de cada n sectores leidos llega con un byte de datos cambiado (sin error).
   CD_ERR=n: uno de cada n sectores de ReadN se cambia por INT5 {stat|5, 04h} (fallo de busqueda) y la unidad deja de
     leer (CD_ERR_LECTURA). CD_ERR80=ms: durante los primeros ms tras Init (o tras el primer ReadN si no hubo Init)
     ReadN y Play responden INT5 {stat|1, 80h} (aun no lista: CD_NO_LISTA). Es una prueba de aguante: el HC05 y
     DuckStation solo dan 80h a ReadN/Play sin disco o sin TOC, y el driver no lee hasta el INT2 de Init, cuando la
     unidad ya dice que esta lista.
   CD_AP=0|1|2: pausa automatica (INT4) del CD-DA en el INDEX 00 de la pista siguiente (0, lo de siempre), en su
     INDEX 01 (1: 2 s despues) o nunca (2: sigue por la pista siguiente hasta el final del disco). Con 1 y 2,
     CDDA_PASA_PISTA cuando el audio entra en la pista siguiente.
   CD_COLA=1 (o n ciclos; 1 = 8): cola de escritura de la CPU: escrito el banco (1F801800h) por KUSEG (1F8018xxh, no
     BF8018xxh), una lectura de 1F801801-3h en los n ciclos siguientes se hace en el banco anterior. Un acceso por
     KSEG1 o una lectura de 1F801800h vacian la cola (evento CD_COLA cuando cambia lo que se lee de 1F801803h). Es la
     hipotesis de psx-spx (memorymap.md; unpredictablethings.md dice lo contrario para 1F801000h) y supone que la
     escritura aun espera en la cola: como a la entrada de cd_poll, tras los 12 sw del prologo; no mira cuantas
     escrituras hay delante.
   T1_GLITCH=n: lecturas rotas del temporizador 1 (1F801110h; psx-spx timers.md): una lectura solo puede salir rota si
     coincide (menos de un ciclo) con un cambio del contador con acarreo; una de cada n de esas sale con el bit mas
     alto del acarreo aun sin cambiar (00FFh->0100h se lee 0000h; 1237h->1238h, 1230h): menor que la anterior
     (T1_ROTO).
   LINE_CYC=f: ciclos de CPU por linea (2152 por defecto; 2172.7 = video NTSC en una consola PAL).
   CD_INIT_MS=ms: la segunda respuesta (INT2) de Init a los ms del comando (por defecto unos 120 ms).
   LOADER_IRQ=1 (o n: el primer vblank en el ciclo n): el EXE empieza como tras el LoadExec del menu de PSIO, con las
     interrupciones activadas: SR=401h (IEc e IM2), I_MASK=1 (VBlank), y el vector 80000080h salta a una rutina que el
     menu dejo en lo que es la BSS del EXE (LOADER_IRQ_PC=dir; por defecto __bss_start+40h) que reconoce el VBlank y
     vuelve. psxprof entrega entonces las interrupciones (con SR.IEc, SR.IM2 e I_STAT & I_MASK); si llega una cuando
     crt0 ya ha borrado esa rutina se ejecutan ceros de la BSS: lo dice (IRQ_CUELGUE) y para. LOADER_SP=dir: $sp y
     $fp al empezar (801FFFF0h por defecto; crt0 no los toca: el cargador puede dejarlos en la BSS).
   ODE=xstation|psio: perfiles (cada variable puesta a mano manda sobre el perfil):
     xstation: CD_STAT=1 CD_PAUSA_REAL=1 CD_SEEK_MUL=1.3 CD_SEEK_JIT=80 CD_INT3_RND=1 CD_BUSY=1 CD_GLITCH=1000
       CD_GLITCH5=100 (HC05 de verdad en placas PU-7/PU-8/PU-18, las del fallo de HINTSTS; tasas de psx-spx)
     psio: CD_STAT=1 CD_SEEK_MUL=0.05 CD_SPEED_MS=0 CD_INIT_MS=5 CD_AP=1 CD_BUSY=1 (FPGA: sin busquedas ni arranque
       del motor, respuestas fijas; no se sabe en que indice da PSIO la pausa automatica: CD_AP=1 es el caso malo) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64; typedef int64_t s64;
typedef int16_t s16; typedef int8_t s8;

#define CLK 33868800.0
static double LINE_CYC = 2152.0;      /* NTSC: 3413 ciclos de video por linea */
static int LINES = 263;
static u64 frame_cyc;
static int RAM_LD = 6, ICF_BASE = 0, ICF_WORD = 1, IO_GPU = 2, IO_CD = 8, IO_SPU = 18, IO_MISC = 2;

static u8 ram[0x200000], scratch[0x400];
static u32 R[32], PC, NPC, HI, LO, SR, CAUSE, EPC;
static int ld_r; static u32 ld_v;
static u64 cyc, muldiv_ready, stall_cyc;
static u32 icache_tag[256], icache_valid[256];
static u32 cache_ctrl;
static int halted;
static const char *outdir = ".";

/* ---- perfil */
static u64 *prof;                     /* ciclos por palabra de RAM (PC) */
static u64 *prof_n;                   /* instrucciones ejecutadas */
static u32 sym_vblank, sym_rc_run, sym_rc_run_end;
static u64 prof_from_cyc = 0;
static int prof_on = 1;

/* ---- interrupciones */
static u32 I_STAT, I_MASK;
/* ---- temporizadores */
static u32 t1_mode; static u64 t1_base;
static u32 t0_mode, t2_mode; static u64 t0_base, t2_base;
/* ---- video */
static u64 next_vbl; static u64 vbl_count;
/* ---- DMA */
static u32 DPCR = 0x07654321, DICR;
static u32 dma_madr[7], dma_bcr[7], dma_chcr[7];
static u64 dma_done[7];
/* ---- GPU */
static u16 vram[512][1024];
static u64 gpu_busy_until;
static u32 gpu_fifo[64]; static int gpu_fn, gpu_need;
static u32 gpu_upload_x, gpu_upload_y, gpu_upload_w, gpu_upload_h, gpu_upload_left, gpu_upload_pos;
static u32 tpage = 0, twin = 0, da_x0, da_y0, da_x1 = 1023, da_y1 = 511; static s32 off_x, off_y;
static u32 disp_x, disp_y, disp_mode, disp_on;
static u64 gpu_ticks_total, gpu_prims;
static u64 ll_start, ll_end;          /* ultima lista enlazada a la GPU: cuando se mando y cuando acabo de dibujarse */
/* ---- CD */
static FILE *cdf; static long cd_sectors;
static int ntracks; static u32 trk_lba[100], trk_end[100], trk_idx0[100];
static u8 cd_idx, cd_ie = 0x1F, cd_if;
static u8 cd_param[16]; static int cd_np;
static u8 cd_resp[16]; static int cd_nresp, cd_rpos;
static u8 cd_data[2352]; static int cd_dpos, cd_dlen, cd_dsize = 2048;
static u8 cd_req;
static int cd_stat = 0x02;          /* motor encendido */
static u32 cd_loc, cd_cur; static int cd_mode;
typedef struct { u64 t; u8 irq; u8 resp[8]; u8 n; int kind; } cdev_t;
#define K_RESP 0
#define K_SECTOR 1
static cdev_t cdq[16]; static int cdqn;
static int cd_reading, cd_playing; static u64 cd_next_sector;
/* CD_VIEJOS=n: tras un ReadN sin Pause llegan aun n sectores de donde se leia (como en la unidad real) */
static u32 cd_old_lba; static int cd_old_n; static u64 cd_old_t;
static u8 cd_sector_buf[2352]; static int cd_sector_ready;
static u64 cd_busy_until;
/* velocidad del motor (cambiarla tarda, como en DuckStation: 0,6 s a doble y 1 s a normal) y audio CD-DA:
   posicion por tiempo desde que acaba la busqueda, pausa automatica (INT4) al final de la pista */
static int cd_2x; static u64 cd_speed_ready; static u32 cd_play_lba; static int cd_play_trk;
static FILE *evf;
/* ---- modelos del hardware real (ver la cabecera; todos a 0: como siempre) */
static int o_stat, o_pausa, o_int3, o_busy, o_glitch, o_glitch5, o_err, o_ap, o_cola, o_t1g; static long o_irq, o_glvent;
static double o_seek_mul = 1, o_seek_jit, o_speed_ms = -1, o_err80, o_init_ms = -1;
static const char *ode_pre;            /* perfil ODE=...: valores para las variables que no esten puestas */
static const char *cfg(const char *n)
{
    static char b[32]; const char *e = getenv(n), *p; size_t l = strlen(n);
    if (e || !ode_pre) return e;
    for (p = ode_pre; (p = strstr(p, n)); p += l) if ((p == ode_pre || p[-1] == ' ') && p[l] == '=') { sscanf(p + l + 1, "%31s", b); return b; }
    return 0;
}
static double cfgf(const char *n, double d) { const char *v = cfg(n); return v ? atof(v) : d; }
static u32 o_rng = 0x2545F491;         /* xorshift: el mismo azar en cada ejecucion */
static u32 rnd(void) { o_rng ^= o_rng << 13; o_rng ^= o_rng >> 17; o_rng ^= o_rng << 5; return o_rng; }
static u64 rnd_entre(u64 a, u64 b) { return a + rnd() % (b - a + 1); }
/* busqueda en curso (de Play, ReadN o SeekL/P): de cd_seek_from a cd_seek_to entre los ciclos t0 y t1 (antes de
   t0 se espera al cambio de velocidad) */
static u64 cd_seek_t0, cd_seek_t1; static u32 cd_seek_from, cd_seek_to; static int cd_seek_ex;
static int cd_ocupado, cd_ocupado_cmd, cd_primera;                     /* CD_BUSY: comandos sin primera respuesta */
static int cd_gl_arm; static u64 cd_gl_t; static long cd_gl_n3, cd_gl_n5, cd_gl_e3, cd_gl_e5, cd_gl_hechos;   /* CD_GLITCH */
static long cd_err_n; static int cd_e80_on; static u64 cd_e80_t;       /* CD_ERR, CD_ERR80 */
static int cd_ap_paso;                                                 /* CD_AP: ya se aviso del cambio de pista */
static u64 cola_t; static u8 cola_idx; static long cola_n;            /* CD_COLA */
static long t1_lect, t1_rotas;                                         /* T1_GLITCH: lecturas al cambiar con acarreo, rotas */
/* ---- SPU */
static u16 spu[0x200];
static u8 spuram[0x80000];
static u32 spu_addr;
static void eventf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* teclas de la SPU como el nucleo PSX de MiSTer (rtl/spu.vhd): escribir KON/KOFF SUSTITUYE la mitad del registro
   (no suma bits) y cada voz mira sus bits una vez por muestra (24 voces repartidas en los 768 ciclos de una muestra a
   44,1 kHz): primero KOFF (release) y luego KON (empieza). Un bit pisado por otra escritura antes de que su voz lo
   vea se pierde (SPU_PERDIDA). El fin de cada voz se calcula con la bandera de fin de bloque del ADPCM y el tono */
static u32 spu_kon, spu_koff; static u64 spu_t; static u64 voz_fin[24]; static int voz_on[24];
static long spu_perdidas;
/* psxprof2: cada voz recorre sus bloques ADPCM con el tono (28 muestras por bloque a 44,1 kHz x tono / 4096): bandera 4
   (inicio de bucle) copia la direccion del bloque al registro de repeticion al empezarlo; bandera 1 (fin) pone ENDX
   y, con la 2, salta a la direccion de repeticion (si no, la voz se para); el registro de repeticion lo escribe el juego
   cuando quiere (encadenado, diseno 5.2). ENDX se borra con KON (y al escribir el registro solo con ENDX_BORRA=1:
   Mednafen; en MiSTer no). IRQ de la SPU (SPUCNT bit 6) cuando una voz empieza el bloque de la direccion del IRQ.
   Sucesos SPU_SALTO / SPU_FIN; cifra de saltos */
static u32 voz_cur[24]; static u64 voz_tblk[24]; static u32 spu_endx_bits; static long spu_saltos; static int endx_borra = -1, spu_irq;
static u64 voz_p0[24];                 /* ciclo en que el tono de la voz se puso a 0 (la voz no avanza: pausa de ps1/snd.c) */
static u64 blk_dur(int v) { u32 pitch = spu[v * 8 + 2]; return pitch ? (u64)28 * 4096 / pitch * 768 : (u64)1 << 60; }
static void voz_avanza(int v, u64 hasta)
{
    while (voz_on[v]) {
        u64 d = blk_dur(v); u8 f;
        if (voz_tblk[v] + d > hasta) break;
        voz_tblk[v] += d; f = spuram[(voz_cur[v] + 1) & 0x7FFFF];
        if (f & 1) {
            spu_endx_bits |= 1u << v;
            if (f & 2) { u32 a = (spu[v * 8 + 7] * 8) & 0x7FFFF; spu_saltos++; eventf("SPU_SALTO voz %d %u -> %u", v, voz_cur[v], a); voz_cur[v] = a; }
            else { voz_on[v] = 0; voz_fin[v] = voz_tblk[v]; eventf("SPU_FIN voz %d %u", v, voz_cur[v]); break; }
        } else voz_cur[v] = (voz_cur[v] + 16) & 0x7FFFF;
        if (spuram[(voz_cur[v] + 1) & 0x7FFFF] & 4) spu[v * 8 + 7] = (u16)(voz_cur[v] >> 3);   /* inicio de bucle */
        if ((spu[(0xDAA - 0xC00) / 2] & 0x40) && voz_cur[v] == (u32)(spu[(0xDA4 - 0xC00) / 2] * 8)) { I_STAT |= 0x200; spu_irq = 1; }   /* IRQ de la SPU */
    }
}
static void spu_avanza(void)
{
    int v;
    /* procesar las voces con bits pendientes hasta el ciclo actual */
    while (spu_kon | spu_koff) {
        u64 base = spu_t - spu_t % 768; int hecho = 0;
        for (v = 0; v < 24; v++) {
            u64 t = base + v * 32;
            if (t < spu_t) continue;
            if (t > cyc) { hecho = 1; break; }
            spu_t = t + 1;
            if (spu_koff & (1u << v)) { spu_koff &= ~(1u << v); if (voz_on[v]) { voz_on[v] = 0; voz_fin[v] = t; eventf("SPU_OFF voz %d", v); } }
            if (spu_kon & (1u << v)) {
                char ant[48] = "";
                spu_kon &= ~(1u << v);
                voz_avanza(v, t);
                /* la voz sonaba: corte; si no, silencio desde que acabo (hueco) */
                if (voz_on[v]) snprintf(ant, sizeof ant, " corte");
                else if (voz_fin[v]) snprintf(ant, sizeof ant, " hueco_ms %.1f", (t - voz_fin[v]) * 1000.0 / CLK);
                voz_on[v] = 1; voz_cur[v] = (spu[v * 8 + 3] * 8) & 0x7FFFF; voz_tblk[v] = t; spu_endx_bits &= ~(1u << v);
                if (spuram[(voz_cur[v] + 1) & 0x7FFFF] & 4) spu[v * 8 + 7] = (u16)(voz_cur[v] >> 3);
                eventf("SPU_ON voz %d addr %u%s", v, voz_cur[v], ant);
            }
        }
        if (hecho) break;
        if (v == 24) spu_t = base + 768;
    }
    if (!(spu_kon | spu_koff)) spu_t = cyc;
    for (v = 0; v < 24; v++) voz_avanza(v, cyc);
}
static u32 spu_endx(void)
{
    spu_avanza();
    return spu_endx_bits;
}
/* WAV=fichero: el sonido (modelo sencillo, ver la cabecera). Se rinde hasta el ciclo actual antes de cada escritura a
   la SPU y al acabar */
static FILE *wavf; static u64 wav_n, wav_t;
static struct { int on, rel; u32 addr, cnt; s16 b[28], h1, h2; int env; } wv[24];
static void wav_bloque(int v)
{
    static const int f0[5] = {0, 60, 115, 98, 122}, f1[5] = {0, 0, -52, -55, -60};
    const u8 *p = &spuram[wv[v].addr & 0x7FFFF]; int sh = p[0] & 15, fi = (p[0] >> 4) % 5, i;
    if (sh > 12) sh = 9;
    for (i = 0; i < 28; i++) {
        int n = (p[2 + i / 2] >> ((i & 1) * 4)) & 15, x = (s16)(n << 12) >> sh;
        x += (wv[v].h1 * f0[fi] + wv[v].h2 * f1[fi] + 32) >> 6;
        if (x > 32767) x = 32767; if (x < -32768) x = -32768;
        wv[v].h2 = wv[v].h1; wv[v].h1 = (s16)x; wv[v].b[i] = (s16)x;
    }
    if (p[1] & 4) spu[v * 8 + 7] = (u16)((wv[v].addr & 0x7FFFF) >> 3);
}
static void wav_hasta(u64 hasta)
{
    if (!wavf) return;
    for (; wav_t + 768 <= hasta; wav_t += 768) {
        int v, acc = 0; s16 o;
        for (v = 0; v < 24; v++) {
            int i, a, b, s;
            if (!wv[v].on) continue;
            i = wv[v].cnt >> 12;
            a = wv[v].b[i]; b = i < 27 ? wv[v].b[i + 1] : a;
            s = a + (b - a) * (int)(wv[v].cnt & 0xFFF) / 4096;
            acc += s * (s16)spu[v * 8] / 0x4000 * wv[v].env / 0x7FFF;
            if (wv[v].rel && (wv[v].env -= 256) <= 0) { wv[v].on = 0; continue; }
            wv[v].cnt += spu[v * 8 + 2] > 0x4000 ? 0x4000 : spu[v * 8 + 2];
            while (wv[v].cnt >= 28 << 12 && wv[v].on) {
                u8 f = spuram[(wv[v].addr + 1) & 0x7FFFF];
                wv[v].cnt -= 28 << 12;
                if (f & 1) { if (f & 2) wv[v].addr = spu[v * 8 + 7] * 8u; else { wv[v].on = 0; break; } }
                else wv[v].addr += 16;
                wav_bloque(v);
            }
        }
        acc = acc * (s16)spu[(0xD80 - 0xC00) / 2] / 0x4000;
        o = (s16)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
        fwrite(&o, 2, 1, wavf); wav_n++;
    }
}
static void wav_teclas(u32 on, u32 off)
{
    int v;
    for (v = 0; v < 24; v++) {
        if (off & (1u << v)) wv[v].rel = 1;
        if (on & (1u << v)) { wv[v].on = 1; wv[v].rel = 0; wv[v].env = 0x7FFF; wv[v].cnt = 0; wv[v].h1 = wv[v].h2 = 0; wv[v].addr = spu[v * 8 + 3] * 8u; wav_bloque(v); }
    }
}
static void wav_cierra(void)
{
    u32 n = (u32)wav_n * 2, h[11] = {0x46464952, 36 + n, 0x45564157, 0x20746d66, 16, 0x10001, 44100, 88200, 0x100002, 0x61746164, n};
    if (!wavf) return;
    fseek(wavf, 0, SEEK_SET); fwrite(h, 4, 11, wavf); fclose(wavf); wavf = 0;
}
/* ---- mando */
static int joy_state, joy_byte_pending; static u64 joy_rx_at, joy_ack_at; static u8 joy_rx; static int joy_rx_valid;
static u64 joy_tx_busy_until; static u16 joy_ctrl, joy_mode, joy_baud; static int joy_seq; static u16 pad_buttons = 0xFFFF;
/* ---- juego */
static long game_frame;               /* llamadas a rc_vblank */
static long max_game_frames = 3000;
static FILE *framef;
static u64 last_vblank_call;
static long shot_every = 0, prof_from_frame = 0;
static u32 sym_ram, sym_vram, sym_pal, sym_spr, sym_vregs, sym_xram, sym_mcu;
/* psxprof2: tiempos por llamada (entrada por el simbolo, salida por la direccion de vuelta de la entrada) */
static u64 t_vbin, t_vbout, t_min, vb_cyc, mcu_cyc; static u32 ra_vb, ra_mcu, sym_rcr, sym_rcdl; static long frame68; static FILE *huellaf, *cpuf;
/* cambio de bufer (GP1 05): lineas desde que empezo el vblank (si se pasa, la parte de arriba sale rota) */
static u64 vbl_set_cyc; static long swap_hist[64]; static double swap_max; static long swap_max_fr;
static u32 minsp = 0x1FFFFF; static long minsp_fr;
static u32 minsp_spad = 0x400, minsp_spad_pc; static long minsp_spad_fr;   /* pila en el scratchpad (PILA_SPAD) */
static u32 fnv(u32 h, u32 a, u32 n) { u32 i; a &= 0x1FFFFF; for (i = 0; i < n; i++) { h ^= ram[(a + i) & 0x1FFFFF]; h *= 16777619u; } return h; }
static u32 state_hash(void) { u32 h = 2166136261u; if (!sym_ram) return 0; h = fnv(h, sym_ram, 0x10000); h = fnv(h, sym_vram, 0x10000); h = fnv(h, sym_pal, 0x2000); h = fnv(h, sym_xram, 0x2000); return h; }   /* psxprof2: como rc_host HUELLA */

#include <stdarg.h>
static void eventf(const char *fmt, ...)
{
    va_list ap;
    if (!evf) return;
    fprintf(evf, "%llu %ld %llu ", (unsigned long long)cyc, game_frame, (unsigned long long)vbl_count);
    va_start(ap, fmt); vfprintf(evf, fmt, ap); va_end(ap);
    fputc('\n', evf);
}

/* ---------------------------------------------------------------- entradas */
/* escenarios (variables de entorno):
   por defecto  moneda en 600, START en 800/1000/1200, X (acelerar) desde 1300 (como rc_host)
   NOCOIN1=1    sin esa primera moneda (solo la demostracion del attract)
   COIN2=N      moneda en N y START en N+200/400/600 (segunda partida, o la primera tras el attract con NOCOIN1:
                entonces X desde N+700)
   STEER=1      direccion oscilante desde 1300 (90 cuadros recto, derecha, recto, izquierda): choques y derrapes;
                STEER=n (n > 1): con n cuadros por tramo
   INPUTS=a-b:BOT,...  botones de mas en los cuadros [a, b) (a sola: solo ese cuadro; a-b%P/W: solo cuando
                (cuadro - a) % P < W): C moneda, S START, X acelerar, x soltar el acelerador, Q cuadrado, 1 L1, 2 R1, cruceta
                R L U D. Elegir rally en 'Choose a Rally': R en 1180-1190 Monte-Carlo, R tres veces Acropolis,
                L 1000 Lagos (lo mismo que rc_host)
   RND=semilla  direccion al azar desde RNDFROM (1300) en tramos de 5 a 120 cuadros
   POKE68=dir:valor[:desde],...  palabra del 68000 (dir y valor en hex) puesta a valor en cada cuadro desde 'desde'.
                POKE68=FEC1AA:0:1900,FEFF08:0:1900 para el reloj de la carrera (no hay 'Time Exceeded'): con STEER=1
                se llega a la meta de Sanremo ('Your Time') hacia el cuadro 11400 y sigue el tramo 2 hacia el 12500 */
/* a-b[%P/W]:botones,... (cuadros a..b-1; con %P/W solo W de cada P): C moneda, S START, X acelerar, Q marcha, L R U D, 1 L1, 2 R1, x soltar X
   psxprof2: G = nivel de la marcha del banco (larga mientras esta); el cuadrado se pulsa un cuadro al cambiar de nivel */
static int g_lvl;
static u16 inputs_spec(const char *q, long fr, u16 in)
{
    for (; q && *q; ) {
        long a = strtol(q, (char **)&q, 10), b = a + 1, P = 0, Wd = 0; const char *bt, *c;
        if (*q == '-') b = strtol(q + 1, (char **)&q, 10);
        if (*q == '%') { P = strtol(q + 1, (char **)&q, 10); if (*q == '/') Wd = strtol(q + 1, (char **)&q, 10); }
        if (*q == ':') q++;
        for (bt = q; *q && *q != ','; q++);
        if (fr >= a && fr < b && (!P || (fr - a) % P < Wd)) for (c = bt; c < q; c++) switch (*c) {
            case 'C': in &= ~0x0001; break; case 'S': in &= ~0x0008; break; case 'X': in &= ~0x4000; break;
            case 'Q': in &= ~0x8000; break; case 'G': g_lvl = 1; break; case 'L': in &= ~0x0080; break; case 'R': in &= ~0x0020; break;
            case 'U': in &= ~0x0010; break; case 'D': in &= ~0x0040; break; case '1': in &= ~0x0400; break; case '2': in &= ~0x0800; break;   /* L1, R1 */ case 'x': in |= 0x4000; break;
            }
        if (*q == ',') q++;
    }
    return in;
}
static u16 inputs_for(long fr)
{
    static int init, nc1, steer; static long c2 = -1, seed, nxt; static int cur;
    const char *q;
    u16 in = 0xFFFF;   /* botones del mando, activos a 0 */
    if (!init) {
        init = 1; nc1 = getenv("COIN1") == 0; if (getenv("COIN2")) c2 = atol(getenv("COIN2"));   /* psxprof2: sin moneda si no se pide (COIN1=1) */
        if (getenv("STEER")) { steer = atoi(getenv("STEER")); if (steer <= 1) steer = 90; }
        seed = getenv("RND") ? atol(getenv("RND")) : 0;
    }
    if (!nc1) {
        if (fr >= 600 && fr < 606) in &= ~0x0001;                   /* SELECT: moneda */
        if ((fr >= 800 && fr < 810) || (fr >= 1000 && fr < 1010) || (fr >= 1200 && fr < 1210)) in &= ~0x0008;   /* START */
        if (fr >= 1300) in &= ~0x4000;                               /* X: acelerar */
    }
    if (c2 > 0) {
        long k = fr - c2;
        if (k >= 0 && k < 6) in &= ~0x0001;
        if ((k >= 200 && k < 210) || (k >= 400 && k < 410) || (k >= 600 && k < 610)) in &= ~0x0008;
        if (nc1 && k >= 700) in &= ~0x4000;
    }
    if (steer && fr >= 1300) {   /* direccion: oscilar para que el coche no vaya recto siempre */
        long ph = ((fr - 1300) / steer) % 4;
        if (ph == 1) in &= ~0x0020; else if (ph == 3) in &= ~0x0080;
    }
    g_lvl = 0;
    in = inputs_spec(getenv("INPUTS"), fr, in);
    { static int g_prev; if (g_lvl != g_prev) in &= ~0x8000; g_prev = g_lvl; }
    if (seed && fr >= (getenv("RNDFROM") ? atol(getenv("RNDFROM")) : 1300)) {
        if (fr >= nxt) { seed = seed * 1103515245 + 12345; cur = (seed >> 16) % 3; seed = seed * 1103515245 + 12345; nxt = fr + 5 + ((seed >> 16) & 0x7FFF) % 116; }
        if (cur == 1) in &= ~0x0020; else if (cur == 2) in &= ~0x0080;
    }
    return in;
}

/* ---------------------------------------------------------------- GPU */
static u16 c24to15(u32 c) { return ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10); }
static void plot(int x, int y, u16 c, int semi, int mode)
{
    if (x < (int)da_x0 || x > (int)da_x1 || y < (int)da_y0 || y > (int)da_y1) return;
    if (x < 0 || x >= 1024 || y < 0 || y >= 512) return;
    if (semi) {
        u16 b = vram[y][x]; int br = b & 31, bg = (b >> 5) & 31, bb = (b >> 10) & 31, fr = c & 31, fg = (c >> 5) & 31, fb = (c >> 10) & 31, r, g, bl;
        switch (mode) {
        case 0: r = (br + fr) / 2; g = (bg + fg) / 2; bl = (bb + fb) / 2; break;
        case 1: r = br + fr; g = bg + fg; bl = bb + fb; break;
        case 2: r = br - fr; g = bg - fg; bl = bb - fb; break;
        default: r = br + fr / 4; g = bg + fg / 4; bl = bb + fb / 4; break;
        }
        r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 31 ? 31 : g; bl = bl < 0 ? 0 : bl > 31 ? 31 : bl;
        c = (c & 0x8000) | r | (g << 5) | (bl << 10);
    }
    vram[y][x] = c;
}
/* CLUTCACHE=1: cache de la CLUT de una entrada como la GPU (se recarga al cambiar de CLUT o de profundidad, y la
   vacia GP0(01h)); CLUTCACHE=2: igual pero GP0(01h) no la vacia (las escrituras en la VRAM nunca la vacian) */
static int clutcache = -1; static u32 ck_key = ~0u; static u16 ck[256];
static u16 clut_entry(u32 clut, int depth, int idx)
{
    if (clutcache < 0) { const char *e = getenv("CLUTCACHE"); clutcache = e ? atoi(e) : 0; }
    if (!clutcache) return vram[(clut >> 6) & 511][((clut & 63) * 16 + idx) & 1023];
    if (ck_key != (clut | (u32)depth << 16)) {
        int k; ck_key = clut | (u32)depth << 16;
        for (k = 0; k < (depth ? 256 : 16); k++) ck[k] = vram[(clut >> 6) & 511][((clut & 63) * 16 + k) & 1023];
    }
    return ck[idx];
}
static int texel(u32 tp, u32 clut, int u, int v, u16 *out)
{
    int tx = (tp & 15) * 64, ty = ((tp >> 4) & 1) * 256, depth = (tp >> 7) & 3;
    int mx = (twin & 31) * 8, my = ((twin >> 5) & 31) * 8, ox = ((twin >> 10) & 31) * 8, oy = ((twin >> 15) & 31) * 8;
    u16 t;
    u &= 255; v &= 255;
    u = (u & ~mx) | (ox & mx); v = (v & ~my) | (oy & my);
    if (depth == 0) {
        u16 w = vram[(ty + v) & 511][(tx + u / 4) & 1023];
        int idx = (w >> ((u & 3) * 4)) & 15;
        t = clut_entry(clut, 0, idx);
    } else if (depth == 1) {
        u16 w = vram[(ty + v) & 511][(tx + u / 2) & 1023];
        int idx = (w >> ((u & 1) * 8)) & 255;
        t = clut_entry(clut, 1, idx);
    } else t = vram[(ty + v) & 511][(tx + u) & 1023];
    if (!t) return 0;
    *out = t; return 1;
}
static void draw_rect(int x, int y, int w, int h, int textured, int semi, u32 col, u32 clut, int u0, int v0)
{
    int i, j, mode = (tpage >> 5) & 3;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) {
        if (textured) {
            u16 t;
            if (!texel(tpage, clut, u0 + i, v0 + j, &t)) continue;
            plot(x + i, y + j, t, semi && (t & 0x8000), mode);
        } else plot(x + i, y + j, c24to15(col), semi, mode);
    }
}
static void draw_quad_tex(s32 *xs, s32 *ys, int *us, int *vs, u32 clut, u32 tp, int semi)
{
    /* solo cuadrilateros alineados a los ejes (los que usa el port): interpolacion lineal de u/v */
    int x0 = xs[0], y0 = ys[0], x1 = xs[3], y1 = ys[3], i, j, mode = (tp >> 5) & 3;
    u32 savetp = tpage; tpage = (tpage & ~0x9FF) | (tp & 0x9FF);
    if (x1 <= x0 || y1 <= y0) { tpage = savetp; return; }
    for (j = y0; j < y1; j++) for (i = x0; i < x1; i++) {
        int u = us[0] + (us[1] - us[0]) * (i - x0) / (x1 - x0);
        int v = vs[0] + (vs[2] - vs[0]) * (j - y0) / (y1 - y0);
        u16 t;
        if (!texel(tp, clut, u, v, &t)) continue;
        plot(i, j, t, semi && (t & 0x8000), mode);
    }
    tpage = savetp;
}
static void gpu_add_ticks(u64 gt)   /* gt en ciclos de GPU (2 por ciclo de CPU) */
{
    u64 c = (gt + 1) / 2;
    if (gpu_busy_until < cyc) gpu_busy_until = cyc;
    gpu_busy_until += c;
    gpu_ticks_total += gt;
}
static void rect_ticks(int x, int y, int w, int h, int textured, int semi)
{
    int x0 = x < (int)da_x0 ? da_x0 : x, y0 = y < (int)da_y0 ? da_y0 : y, x1 = x + w - 1 > (int)da_x1 ? da_x1 : x + w - 1, y1 = y + h - 1 > (int)da_y1 ? da_y1 : y + h - 1;
    int dw = x1 - x0 + 1, dh = y1 - y0 + 1, tpr;
    if (dw <= 0 || dh <= 0) { gpu_add_ticks(16); return; }
    tpr = dw; if (textured) tpr += dw; if (semi) tpr += (dw + 1) / 2;
    gpu_add_ticks((u64)tpr * dh + 16);
}
static int gp0_len(u32 cmd)
{
    u32 op = cmd >> 24;
    if (op == 0x02) return 3;
    if (op >= 0x20 && op < 0x40) {
        int quad = (op >> 3) & 1, tex = (op >> 2) & 1, gour = (op >> 4) & 1, nv = quad ? 4 : 3;
        return 1 + nv * (1 + tex) + (gour ? nv - 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) return (op & 0x10) ? 4 : 3;   /* lineas simples (polilineas no se usan) */
    if (op >= 0x60 && op < 0x80) {
        int size = (op >> 3) & 3, tex = (op >> 2) & 1;
        return 2 + tex + (size == 0 ? 1 : 0);
    }
    if (op >= 0x80 && op < 0xA0) return 4;
    if (op >= 0xA0 && op < 0xC0) return 3;
    if (op >= 0xC0 && op < 0xE0) return 3;
    return 1;
}
static void gp0_exec(void)
{
    u32 *f = gpu_fifo, op = f[0] >> 24;
    gpu_prims++;
    if (op == 0x01) { if (clutcache == 1) ck_key = ~0u; return; }
    if (op == 0x02) {
        int x = f[1] & 0x3F0, y = (f[1] >> 16) & 0x1FF, w = ((f[2] & 0x3FF) + 15) & ~15, h = (f[2] >> 16) & 0x1FF, i, j;
        u16 c = c24to15(f[0] & 0xFFFFFF);
        for (j = 0; j < h; j++) for (i = 0; i < w; i++) vram[(y + j) & 511][(x + i) & 1023] = c;
        gpu_add_ticks(46 + ((w / 8) + 9) * h);
        return;
    }
    if (op >= 0x60 && op < 0x80) {
        int size = (op >> 3) & 3, tex = (op >> 2) & 1, semi = (op >> 1) & 1, k = 1;
        s32 x = (s16)(f[k] << 5) >> 5, y = (s16)((f[k] >> 16) << 5) >> 5; u32 clut = 0; int u = 0, v = 0, w, h;
        k++;
        if (tex) { u = f[k] & 255; v = (f[k] >> 8) & 255; clut = f[k] >> 16; k++; }
        if (size == 0) { w = f[k] & 0x3FF; h = (f[k] >> 16) & 0x1FF; } else if (size == 1) w = h = 1; else if (size == 2) w = h = 8; else w = h = 16;
        x += off_x; y += off_y;
        draw_rect(x, y, w, h, tex, semi, f[0] & 0xFFFFFF, clut, u, v);
        rect_ticks(x, y, w, h, tex, semi);
        return;
    }
    if (op >= 0x20 && op < 0x40) {
        int quad = (op >> 3) & 1, tex = (op >> 2) & 1, semi = (op >> 1) & 1, gour = (op >> 4) & 1, nv = quad ? 4 : 3, k = 1, i;
        s32 xs[4], ys[4]; int us[4], vs[4]; u32 clut = 0, tp = tpage;
        for (i = 0; i < nv; i++) {
            if (gour && i) k++;
            xs[i] = ((s16)(f[k] << 5) >> 5) + off_x; ys[i] = ((s16)((f[k] >> 16) << 5) >> 5) + off_y; k++;
            if (tex) { us[i] = f[k] & 255; vs[i] = (f[k] >> 8) & 255; if (i == 0) clut = f[k] >> 16; if (i == 1) tp = f[k] >> 16; k++; }
        }
        if (tex && quad) { draw_quad_tex(xs, ys, us, vs, clut, tp, semi); tpage = (tpage & ~0x9FF) | (tp & 0x9FF); }
        {   /* coste: dos triangulos */
            s64 a = llabs((s64)(xs[1] - xs[0]) * (ys[2] - ys[0]) - (s64)(xs[2] - xs[0]) * (ys[1] - ys[0])) / 2, px = a * (quad ? 2 : 1);
            if (tex) px += px; if (semi) px += (px + 1) / 2;
            gpu_add_ticks(px + 64);
        }
        return;
    }
    if (op >= 0x80 && op < 0xA0) {   /* copia de VRAM a VRAM */
        int sx = f[1] & 0x3FF, sy = (f[1] >> 16) & 0x1FF, dx = f[2] & 0x3FF, dy = (f[2] >> 16) & 0x1FF, i, j;
        int w = ((f[3] & 0x3FF) - 1) & 0x3FF, h = (((f[3] >> 16) & 0x1FF) - 1) & 0x1FF; static u16 ln[1024];
        w++; h++;
        for (j = 0; j < h; j++) {
            for (i = 0; i < w; i++) ln[i] = vram[(sy + j) & 511][(sx + i) & 1023];
            for (i = 0; i < w; i++) vram[(dy + j) & 511][(dx + i) & 1023] = ln[i];
        }
        gpu_add_ticks(w * h * 2 + 16);
        return;
    }
    if (op >= 0xA0 && op < 0xC0) {
        gpu_upload_x = f[1] & 0x3FF; gpu_upload_y = (f[1] >> 16) & 0x1FF;
        gpu_upload_w = ((f[2] & 0x3FF) - 1) & 0x3FF; gpu_upload_w++; gpu_upload_h = (((f[2] >> 16) & 0x1FF) - 1) & 0x1FF; gpu_upload_h++;
        gpu_upload_left = (gpu_upload_w * gpu_upload_h + 1) / 2; gpu_upload_pos = 0;
        return;
    }
    switch (op) {
    case 0xE1: tpage = f[0] & 0xFFFFFF; break;
    case 0xE2: twin = f[0] & 0xFFFFF; break;
    case 0xE3: da_x0 = f[0] & 0x3FF; da_y0 = (f[0] >> 10) & 0x1FF; break;
    case 0xE4: da_x1 = f[0] & 0x3FF; da_y1 = (f[0] >> 10) & 0x1FF; break;
    case 0xE5: off_x = (s32)(f[0] << 21) >> 21; off_y = (s32)(f[0] << 10) >> 21; break;
    default: break;
    }
    gpu_add_ticks(2);
}
static void gp0_write(u32 v)
{
    if (gpu_upload_left) {
        int k;
        for (k = 0; k < 2; k++) {
            u32 p = gpu_upload_pos++;
            if (p < gpu_upload_w * gpu_upload_h) vram[(gpu_upload_y + p / gpu_upload_w) & 511][(gpu_upload_x + p % gpu_upload_w) & 1023] = (u16)(k ? v >> 16 : v);
        }
        gpu_upload_left--;
        if (!gpu_upload_left) gpu_add_ticks((gpu_upload_w * gpu_upload_h) / 2 + 16);
        return;
    }
    if (gpu_fn == 0) { gpu_need = gp0_len(v); }
    gpu_fifo[gpu_fn++] = v;
    if (gpu_fn >= gpu_need) { gp0_exec(); gpu_fn = 0; }
}
static void gp1_write(u32 v)
{
    switch (v >> 24) {
    case 0x00: gpu_fn = 0; gpu_upload_left = 0; disp_on = 0; break;
    case 0x01: gpu_fn = 0; gpu_upload_left = 0; break;
    case 0x03: disp_on = !(v & 1); break;
    case 0x05: { void dump_show(void); disp_x = v & 0x3FF; disp_y = (v >> 10) & 0x1FF;
        { double l = (double)(cyc - vbl_set_cyc) / LINE_CYC; int b = l < 0 ? 0 : l >= 63 ? 63 : (int)l; swap_hist[b]++; if (l > swap_max) { swap_max = l; swap_max_fr = game_frame; }
          if (l >= 4 && game_frame > 1) eventf("BUFER_TARDE lineas %.1f", l); }
        dump_show(); break; }
    case 0x08: disp_mode = v & 0xFF; break;
    default: break;
    }
}
static u32 gpustat(void)
{
    u32 s = 0x14802000;
    if (cyc >= gpu_busy_until && !gpu_upload_left) s |= 1 << 26;
    s |= 1 << 28;  /* listo para DMA */
    s |= 1 << 27;
    if ((vbl_count & 1)) s |= 1u << 31;
    return s;
}
static void screenshot(const char *name)
{
    char fn[600]; FILE *f; int x, y, w = 368, h = 240;
    snprintf(fn, sizeof fn, "%s/%s.ppm", outdir, name);
    f = fopen(fn, "wb"); if (!f) return;
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        u16 c = vram[(disp_y + y) & 511][(disp_x + x) & 1023];
        u8 rgb[3] = { (u8)((c & 31) << 3), (u8)(((c >> 5) & 31) << 3), (u8)(((c >> 10) & 31) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* SHOTAT=a,b-c,...: capturas de mas en esos cuadros */
static int shot_at(long fr)
{
    const char *p = getenv("SHOTAT");
    while (p && *p) {
        long a = strtol(p, (char **)&p, 10), b = a;
        if (*p == '-') b = strtol(p + 1, (char **)&p, 10);
        if (fr >= a && fr <= b) return 1;
        if (*p == ',') p++; else break;
    }
    return 0;
}
/* ---------------------------------------------------------------- DMA */
static u32 rd32ram(u32 a) { a &= 0x1FFFFC; return ram[a] | (ram[a + 1] << 8) | (ram[a + 2] << 16) | ((u32)ram[a + 3] << 24); }
static void wr32ram(u32 a, u32 v) { a &= 0x1FFFFC; ram[a] = v; ram[a + 1] = v >> 8; ram[a + 2] = v >> 16; ram[a + 3] = v >> 24; }
static u32 rd16ram(u32 a) { a &= 0x1FFFFE; return ram[a] | (ram[a + 1] << 8); }
/* tiles que se ven y no estan en la cache de VRAM del port (tslot[t] == 0xFFFF) al mandar la lista del cuadro:
   capas (mapa visible de las dos) y sprites visibles, sin repetir. Para ver cuanto faltan y cuanto tardan */
static u32 sym_tslot, sym_loading, sym_in_race; static FILE *misslog;   /* MISSLOG=1: falta.txt con cada tile que falta */
static int miss_lay, miss_spr;
static void count_missing(void)
{
    static u8 seen[16384]; static u16 lst[2048]; int n = 0, L, i, j, nl = 0, ns = 0;
    if (!sym_tslot || !sym_vram) return;
    for (L = 0; L < 2; L++) {
        u32 vr0 = rd16ram(sym_vregs + (L ? 4 : 0)), vr1 = rd16ram(sym_vregs + (L ? 6 : 2));
        int sx = L ? (vr1 + 8) & 1023 : (vr1 + 12) & 1023, sy = (vr0 + 16) & 511;
        int nv = (368 + 15 + (sx & 15)) >> 4, nf = (240 + 15 + (sy & 15)) >> 4;
        u32 m = sym_vram + (L ? 0x2000 : 0);
        for (j = 0; j < nf; j++) for (i = 0; i < nv; i++) {
            u32 t = rd32ram(m + ((((sy >> 4) + j) & 31) * 64 + (((sx >> 4) + i) & 63)) * 4) & 0x3FFF;
            if (seen[t]) continue;
            seen[t] = 1; lst[n++] = t;
            if (rd16ram(sym_tslot + t * 2) == 0xFFFF) { nl++; if (misslog) fprintf(misslog, "%ld L %u\n", game_frame, t); }
        }
    }
    for (i = 0; i < 511; i++) {
        u32 a = sym_spr + 6 + i * 8, w0 = rd16ram(a), w2 = rd16ram(a + 4), t = rd16ram(a + 6) & 0x3FFF;
        int sx = ((w2 & 0x3FF) - 23) & 0x3FF, sy = ((240 - (w0 & 0xFF)) & 0xFF) - 16;
        if (sx >= 0x3F0) sx -= 0x400;
        if (sx >= 368 || sy >= 240 || sx <= -16 || sy <= -16 || seen[t] == 2) continue;
        if (!seen[t]) lst[n++] = t;
        seen[t] = 2;
        if (rd16ram(sym_tslot + t * 2) == 0xFFFF) { ns++; if (misslog) fprintf(misslog, "%ld S %u\n", game_frame, t); }
    }
    for (i = 0; i < n; i++) seen[lst[i]] = 0;
    miss_lay = nl; miss_spr = ns;
}
/* huella de la imagen que se ve (para comparar cuadro a cuadro con otra version) */
static u32 disp_hash(void)
{
    u32 h = 2166136261u; int x, y;
    for (y = 0; y < 240; y++) for (x = 0; x < 368; x++) { h ^= vram[(disp_y + y) & 511][(disp_x + x) & 1023]; h *= 16777619u; }
    return h;
}
/* ---- DUMPAT="f1,f2,a-b/paso": por cuadro f (llamada f a rc_vblank, la numeracion de SHOT e INPUTS) la imagen que
   ensena la PS1 y el estado de la placa con el que se dibujo. La PS1 dibuja en la llamada f (pk_send: la unica lista
   enlazada por DMA) el estado de la placa al entrar en ella (el 68000 no corre entre medias: se comprueba con la
   huella) y lo ensena desde el vsync del siguiente cuadro que se dibuja (GP1 05): ahi se guarda la captura. Si f no
   se dibujo (FRAMESKIP) en su lugar se ve el ultimo dibujado g < f: se guarda ese (saltado = 1) y el estado de f en
   dump_f_placa.bin. Si algo dibuja en el bufer entre la lista y el GP1 05 (la pantalla de carga) imagen_ok = 0.
   Salen dump_NNNNN.ppm (368x240), dump_NNNNN.bin y dumps.csv (formato en perf/arcade.py). El estado de f es el de la
   fila cuadro_juego = f - 1 de cuadros.csv y la linea f de perf/ref_estado.txt */
#define DS_N (0x4000 + 0x4000 + 0x1000 + 0x10)
static u8 *dump_want; static long dump_max; static FILE *dumpf;
static u8 dump_st[4][DS_N]; static u32 dump_h[4]; static long dump_sf[4] = {-1, -1, -1, -1};   /* al entrar en la llamada c: [c & 3] */
typedef struct { long f; int ok; u32 h, ml, ms, ih; u8 pres[2048]; u16 clut[16][288]; } dump_buf_t;
static dump_buf_t dump_b[2] = {{-1}, {-1}};        /* lo dibujado en cada bufer (y 0 / 256) al mandar la lista */
static int dump_w(long f) { return dump_want && f >= 0 && f < dump_max && dump_want[f] == 1; }
static u32 dump_ih(int fy) { u32 h = 2166136261u; int x, y; for (y = 0; y < 240; y++) for (x = 0; x < 368; x++) { h ^= vram[(fy + y) & 511][x]; h *= 16777619u; } return h; }
static void dump_init(void)
{
    const char *p = getenv("DUMPAT"); char fn[600];
    if (!p) return;
    dump_max = max_game_frames + 2; dump_want = calloc(dump_max, 1);
    while (*p) {
        long a = strtol(p, (char **)&p, 10), b = a, st = 1, f;
        if (*p == '-') b = strtol(p + 1, (char **)&p, 10);
        if (*p == '/') st = strtol(p + 1, (char **)&p, 10);
        for (f = a; f <= b && f < dump_max; f += st > 0 ? st : 1) if (f >= 0) dump_want[f] = 1;
        if (*p == ',') p++; else break;
    }
    snprintf(fn, sizeof fn, "%s/dumps.csv", outdir); dumpf = fopen(fn, "w");
    if (dumpf) fprintf(dumpf, "cuadro,dibujado,saltado,llamada_vista,vblank_vista,estado,huella_ok,imagen_ok,falta_capa,falta_spr\n");
}
static void dump_entry(void)          /* al entrar en rc_vblank (game_frame ya es la llamada c) */
{
    long c = game_frame; u8 *d = dump_st[c & 3];
    if (!dump_w(c) && !dump_w(c + 1)) return;
    memcpy(d, ram + (sym_vram & 0x1FFFFF), 0x4000); memcpy(d + 0x4000, ram + (sym_pal & 0x1FFFFF), 0x4000);
    memcpy(d + 0x8000, ram + (sym_spr & 0x1FFFFF), 0x1000); memcpy(d + 0x9000, ram + (sym_vregs & 0x1FFFFF), 0x10);
    dump_sf[c & 3] = c; dump_h[c & 3] = state_hash();
}
static void dump_list(void)           /* lista del cuadro mandada (psxprof la dibuja entera al momento) */
{
    dump_buf_t *q = &dump_b[da_y0 >= 256]; int t, y;
    q->f = game_frame; q->ok = 0;
    if (!dump_w(game_frame) && !dump_w(game_frame + 1)) return;
    q->ok = 1; q->h = state_hash(); q->ml = miss_lay; q->ms = miss_spr; q->ih = dump_ih(da_y0);
    memset(q->pres, 0, sizeof q->pres);                         /* tiles en la cache de VRAM (tslot) */
    if (sym_tslot) for (t = 0; t < 16384; t++) if (rd16ram(sym_tslot + t * 2) != 0xFFFF) q->pres[t >> 3] |= 1 << (t & 7);
    for (y = 0; y < 16; y++) memcpy(q->clut[y], vram[240 + y], 288 * 2);   /* CLUT de las capas, sprites y efectos */
}
static void dump_bin(const char *nm, long f, long g, int fl, const dump_buf_t *q, int slot)
{
    char fn[600]; FILE *o; u32 hd[16] = {0x31445257, 1, f, g, fl, (u32)vbl_count, dump_h[slot], q->h, q->ml, q->ms, q->f, (u32)game_frame};
    snprintf(fn, sizeof fn, "%s/%s.bin", outdir, nm); o = fopen(fn, "wb"); if (!o) return;
    fwrite(hd, 4, 16, o); fwrite(dump_st[slot], 1, DS_N, o); fwrite(q->pres, 1, 2048, o); fwrite(q->clut, 2, 16 * 288, o);
    fclose(o);
}
void dump_show(void)                  /* GP1 05: empieza a verse el bufer de disp_y */
{
    dump_buf_t *q = &dump_b[disp_y >= 256]; long g = q->f, h;
    char nm[64];
    if (!dump_want || g < 0) return;
    for (h = g; h < game_frame; h++) {                          /* g y los que se saltaron despues (se ve g) */
        int fl = h != g, ok;
        if (!dump_w(h)) continue;
        dump_want[h] = 2;
        ok = q->ok && dump_sf[g & 3] == g;
        if (!ok) { fprintf(stderr, "DUMPAT: sin estado del cuadro %ld\n", g); continue; }
        if (dump_h[g & 3] != q->h) fl |= 2;                    /* el estado cambio entre la entrada y la lista */
        if (dump_ih(disp_y) != q->ih) fl |= 8;                 /* algo dibujo en el bufer despues (pantalla de carga) */
        snprintf(nm, sizeof nm, "dump_%05ld", h); screenshot(nm); dump_bin(nm, h, g, fl, q, g & 3);
        if (h != g && dump_sf[h & 3] == h) { snprintf(nm, sizeof nm, "dump_%05ld_placa", h); dump_bin(nm, h, h, 4, q, h & 3); }
        if (dumpf) fprintf(dumpf, "%ld,%ld,%d,%ld,%llu,%08x,%d,%d,%u,%u\n", h, g, h != g, game_frame, (unsigned long long)vbl_count,
                           dump_h[g & 3], !(fl & 2), !(fl & 8), q->ml, q->ms);
    }
}
static void dma_start(int ch)
{
    u32 chcr = dma_chcr[ch], madr = dma_madr[ch] & 0x1FFFFC, mode = (chcr >> 9) & 3, words = 0;
    if ((dma_madr[ch] & 0xFFFFFF) >= 0x800000 && !(ch == 2 && mode == 2)) {   /* la DMA no llega al scratchpad (p. ej. un
                                                     bufer en la pila, si esta alli): en la consola leeria o escribiria otra cosa */
        eventf("DMA_SCRATCHPAD canal %d desde %08x", ch, dma_madr[ch]);
        fprintf(stderr, "DMA_SCRATCHPAD: canal %d con MADR %08x (PC %08x)\n", ch, dma_madr[ch], PC);
    }
    if (ch == 2) {
        if (mode == 2) {   /* lista enlazada */
            u32 a = madr; int guard = 0;
            u64 gstart = gpu_busy_until > cyc ? gpu_busy_until : cyc;
            (void)gstart;
            for (;;) {
                u32 hd = rd32ram(a), n = hd >> 24, i;
                for (i = 0; i < n; i++) gp0_write(rd32ram(a + 4 + i * 4));
                words += n + 1;
                if ((hd & 0x800000) || ++guard > 100000) break;
                a = hd & 0x1FFFFC;
            }
            stall_cyc += words;                    /* el bus lo ocupa la DMA */
            dma_done[2] = (gpu_busy_until > cyc + words ? gpu_busy_until : cyc + words);
            ll_start = cyc; ll_end = dma_done[2];
            count_missing(); dump_list();
        } else {
            u32 bs = dma_bcr[ch] & 0xFFFF, nb = dma_bcr[ch] >> 16, i;
            if (!bs) bs = 0x10000;
            if (mode == 0) nb = 1;
            words = bs * nb;
            if (chcr & 1) for (i = 0; i < words; i++) gp0_write(rd32ram(madr + i * 4));
            stall_cyc += words;
            dma_done[2] = cyc + words;
        }
    } else if (ch == 4) {
        u32 bs = dma_bcr[ch] & 0xFFFF, nb = dma_bcr[ch] >> 16, i;
        words = bs * nb;
        if (chcr & 1) for (i = 0; i < words; i++) { u32 v = rd32ram(madr + i * 4); u32 p = spu_addr & 0x7FFFF; spuram[p] = v; spuram[(p + 1) & 0x7FFFF] = v >> 8; spuram[(p + 2) & 0x7FFFF] = v >> 16; spuram[(p + 3) & 0x7FFFF] = v >> 24; spu_addr += 4; }
        else for (i = 0; i < words; i++) {   /* SPU -> RAM (lectura de vuelta de ps1/snd.c) */
            u32 p = spu_addr & 0x7FFFF;
            wr32ram(madr + i * 4, spuram[p] | spuram[(p + 1) & 0x7FFFF] << 8 | spuram[(p + 2) & 0x7FFFF] << 16 | (u32)spuram[(p + 3) & 0x7FFFF] << 24);
            spu_addr += 4;
        }
        stall_cyc += words * 4;
        dma_done[4] = cyc + words * 4;
    } else if (ch == 6) {   /* OTC */
        u32 n = dma_bcr[ch] & 0xFFFF, i, a = madr;
        for (i = 0; i < n; i++) { wr32ram(a, i == n - 1 ? 0xFFFFFF : (a - 4) & 0x1FFFFF); a -= 4; }
        stall_cyc += n; dma_done[6] = cyc + n;
    } else dma_done[ch] = cyc;
}

/* ---------------------------------------------------------------- CD */
static int bcd2i(u8 v) { return (v >> 4) * 10 + (v & 15); }
static u8 i2bcd(int v) { return (u8)(((v / 10) << 4) | (v % 10)); }
#define K_ACK 2                       /* primera respuesta de un comando (CD_BUSY: ocupado hasta entregarla) */
static void cd_push(u64 dt, u8 irq, const u8 *r, int n, int kind)
{
    cdev_t *e;
    if (cd_primera && kind == K_RESP) { kind = K_ACK; cd_primera = 0; }
    if (cdqn >= 16) { if (kind == K_ACK && cd_ocupado) cd_ocupado--; return; }
    e = &cdq[cdqn++]; e->t = cyc + dt; e->irq = irq; e->n = n; e->kind = kind; memcpy(e->resp, r, n);
}
static u64 seek_base(u32 from, u32 to)
{
    u32 diff = from > to ? from - to : to - from, spt = 9;
    double s;
    u64 tps = cd_2x ? 225792 : 451584;
    if (from < to && diff <= spt) return tps * (diff < 2 ? 2 : diff);
    if (from >= to && (from >= spt ? from - spt : 0) <= to) return tps * ((to - (from >= spt ? from - spt : 0)) < 1 ? 1 : (to - (from >= spt ? from - spt : 0)));
    if (diff < 7200) {
        double sw = 330.0 - 63.1333 * log(fmin(fmax(from / 4500.0, 1.0), 72.0));
        s = diff < sw ? 0.05 : 0.1;
    } else {
        double lmax = 72 * 4500.0;
        s = 0.05 + (0.85 * (log((double)diff) / log(lmax))) * 0.4 + (0.85 * (diff / lmax)) * 0.6;
    }
    return (u64)(s * CLK) + (u64)(CLK / 1500);
}
static u64 seek_ticks(u32 from, u32 to)   /* CD_SEEK_MUL y CD_SEEK_JIT */
{
    u64 t = seek_base(from, to);
    if (o_seek_mul != 1) t = (u64)(t * o_seek_mul);
    if (o_seek_jit > 0) t += rnd() % ((u64)(o_seek_jit * CLK / 1000) + 1);
    return t;
}
/* espera de la primera respuesta: fija, o con CD_INT3_RND entre el minimo y el maximo de psx-spx (3 de cada 4 cerca
   del minimo, 1 en todo el margen: la media queda como la de psx-spx); Init y ReadTOC alrededor de su media */
static u64 cd_ack(int init)
{
    if (!o_int3) return init ? 80000 : 25000;
    if (init) return rnd_entre(0xF820, 2 * 0x13CCE - 0xF820);
    return (rnd() & 3) ? rnd_entre(0x4A73, 2 * 0x4A73) : rnd_entre(0x4A73, 0x3115B);
}
static void cd_read_sector(u32 lba)
{
    if ((long)lba >= cd_sectors) { memset(cd_sector_buf, 0, sizeof cd_sector_buf); return; }
    fseek(cdf, (long)lba * 2352, SEEK_SET);
    if (fread(cd_sector_buf, 1, 2352, cdf) != 2352) memset(cd_sector_buf, 0, 2352);
    {   /* CD_CORRUPTO=n: uno de cada n sectores llega con un byte de datos cambiado (CD-R mal leido, sin INT5) */
        static long n_cor; static int o_cor = -1;
        if (o_cor < 0) o_cor = getenv("CD_CORRUPTO") ? atoi(getenv("CD_CORRUPTO")) : 0;
        if (o_cor > 0 && ++n_cor % o_cor == 0) { cd_sector_buf[24 + (n_cor * 37) % 2048] ^= 0x5A; eventf("CD_CORRUPTO lba %u", lba); }
    }
}
static int track_of(u32 lba) { int t, k = 1; for (t = 1; t <= ntracks; t++) if (trk_lba[t] <= lba || (trk_idx0[t] && trk_idx0[t] <= lba)) k = t; return k; }
/* posicion actual del audio (avanza a 75 sectores/s desde que acaba la busqueda) */
static u32 cd_play_pos(void)
{
    if (cyc < cd_busy_until) return cd_play_lba;
    return cd_play_lba + (u32)((cyc - cd_busy_until) / 451584);
}
static void cd_stop_activity(const char *why)
{
    if (cd_playing) { cd_cur = cd_play_pos(); eventf("CDDA_STOP %s lba %u", why, cd_cur); }
    cd_reading = 0; cd_playing = 0;
    {   int i, j = 0; for (i = 0; i < cdqn; i++) if (cdq[i].kind != K_SECTOR) cdq[j++] = cdq[i]; cdqn = j; }
}
static int cd_buscando(void) { return (cd_playing || cd_reading || cd_seek_ex) && cyc < cd_seek_t1; }
/* stat: fijo (02h, motor) o, con CD_STAT, segun lo que hace la unidad (solo uno de buscar/leer/tocar) */
static u8 cd_st(void)
{
    if (!o_stat) return cd_stat;
    if (cd_buscando()) return cd_stat | 0x40;
    return cd_stat | (cd_playing ? 0x80 : cd_reading ? 0x20 : 0);
}
/* posicion de la cabeza (CD_STAT): durante la busqueda, interpolada desde el origen */
static u32 cd_head(void)
{
    if (cd_buscando()) {
        if (cyc <= cd_seek_t0) return cd_seek_from;
        return cd_seek_from + (s32)(((double)cd_seek_to - cd_seek_from) * (cyc - cd_seek_t0) / (cd_seek_t1 - cd_seek_t0));
    }
    return cd_playing ? cd_play_pos() : cd_cur;
}
/* CD_ERR80: ReadN/Play poco despues de Init (o del primer ReadN) responden INT5 80h (aun no lista) */
static int cd_no_lista(u8 c, u8 st, u64 ack)
{
    u8 e[2];
    if (!cd_e80_on && c != 0x03) { cd_e80_on = 1; cd_e80_t = cyc; }
    if (!o_err80 || cyc - cd_e80_t >= (u64)(o_err80 * CLK / 1000)) return 0;
    e[0] = st | 1; e[1] = 0x80; cd_push(ack, 5, e, 2, K_RESP);
    eventf("CD_NO_LISTA cmd %02x ms %.1f", c, (cyc - cd_e80_t) * 1000.0 / CLK);
    return 1;
}
static void cd_command(u8 c)
{
    u8 r[8]; int n = 0;
    u64 ack = cd_ack(0);
    if (cd_ocupado) eventf("CD_CMD_OCUPADO %02x (el %02x aun no tiene su INT3)", c, cd_ocupado_cmd);
    cd_ocupado += o_busy; cd_ocupado_cmd = c; cd_primera = 1;
    r[0] = cd_st();
    switch (c) {
    case 0x01: cd_push(ack, 3, r, 1, K_RESP); break;                               /* GetStat */
    case 0x02: cd_loc = (bcd2i(cd_param[0]) * 60 + bcd2i(cd_param[1])) * 75 + bcd2i(cd_param[2]) - 150; cd_push(ack, 3, r, 1, K_RESP); break;
    case 0x03: {                                                                    /* Play */
        u64 st, t0;
        if (cd_no_lista(c, r[0], ack)) break;
        if (cd_np && cd_param[0]) { int t = bcd2i(cd_param[0]); if (t >= 1 && t <= ntracks) cd_loc = trk_lba[t]; }
        if (o_stat) cd_cur = cd_head(); else if (cd_playing) cd_cur = cd_play_pos();
        st = seek_ticks(cd_cur, cd_loc); cd_seek_from = cd_cur;
        cd_stop_activity("play");
        cd_push(ack, 3, r, 1, K_RESP);
        cd_playing = 1; cd_cur = cd_play_lba = cd_loc; cd_play_trk = track_of(cd_loc);
        t0 = cyc > cd_speed_ready ? cyc : cd_speed_ready;
        eventf("CDDA_PLAY lba %u seek_ms %.1f%s", cd_loc, (t0 + st - cyc) * 1000.0 / CLK, (cd_mode & 0x80) ? " A_DOBLE_VELOCIDAD" : "");
        cd_busy_until = t0 + st;
        cd_seek_t0 = t0; cd_seek_t1 = t0 + st; cd_seek_to = cd_loc; cd_seek_ex = 0; cd_ap_paso = 0;
        break; }
    case 0x06: case 0x1B: {                                                         /* ReadN / ReadS */
        u64 st, t0;
        if (cd_no_lista(c, r[0], ack)) break;
        if (o_stat) cd_cur = cd_head(); else if (cd_playing) cd_cur = cd_play_pos();
        st = seek_ticks(cd_cur, cd_loc); cd_seek_from = cd_cur;
        if (cd_playing) eventf("CDDA_STOP read lba %u", cd_cur);
        cd_playing = 0;
        cd_push(ack, 3, r, 1, K_RESP);
        if (cd_reading && getenv("CD_VIEJOS")) { cd_old_lba = cd_cur; cd_old_n = atoi(getenv("CD_VIEJOS")); cd_old_t = cyc + ack + 1000; }
        cd_reading = 1; cd_cur = cd_loc;
        t0 = cyc + ack > cd_speed_ready ? cyc + ack : cd_speed_ready;
        cd_next_sector = t0 + st;
        cd_seek_t0 = t0; cd_seek_t1 = t0 + st; cd_seek_to = cd_loc; cd_seek_ex = 0;
        eventf("CD_READ lba %u seek_ms %.1f x%d", cd_loc, (t0 + st - cyc) * 1000.0 / CLK, cd_2x ? 2 : 1);
        break; }
    case 0x09: {                                                                    /* Pause */
        u64 pt = (cd_reading || cd_playing) ? 9 * 225792ULL : 7400;
        if (o_pausa && cd_buscando()) {       /* CD_PAUSA_REAL: durante la busqueda se rechaza y la unidad sigue */
            u8 e[2]; e[0] = r[0] | 1; e[1] = 0x80; cd_push(ack, 5, e, 2, K_RESP);
            eventf("CD_PAUSE_RECHAZADA %s lba %u faltan_ms %.1f", cd_playing ? "play" : cd_reading ? "read" : "seek", cd_seek_to, (cd_seek_t1 - cyc) * 1000.0 / CLK);
            break;
        }
        cd_old_n = 0; cd_seek_ex = 0;
        if (pt < 1000000 && (cd_reading || cd_playing)) pt = 1000000;
        if (cd_playing) { cd_cur = cd_play_pos(); eventf("CDDA_STOP pause lba %u", cd_cur); }
        cd_reading = 0; cd_playing = 0;
        { int i, j = 0; for (i = 0; i < cdqn; i++) if (cdq[i].kind != K_SECTOR) cdq[j++] = cdq[i]; cdqn = j; }
        cd_push(ack, 3, r, 1, K_RESP);
        if (o_stat) r[0] = cd_stat;           /* la segunda ya sin leer ni tocar */
        cd_push(ack + pt, 2, r, 1, K_RESP);
        eventf("CD_PAUSE ms %.1f", pt * 1000.0 / CLK);
        break; }
    case 0x0A: {                                                                    /* Init */
        u64 ia = cd_ack(1), i2 = o_init_ms < 0 ? 80000 + 4000000 : (u64)(o_init_ms * CLK / 1000);
        if (i2 < ia + 1000) i2 = ia + 1000;
        cd_push(ia, 3, r, 1, K_RESP); if (o_stat) r[0] = cd_stat; cd_push(i2, 2, r, 1, K_RESP); cd_mode = 0x20; cd_dsize = 0x924;
        cd_e80_on = 1; cd_e80_t = cyc;
        if (o_init_ms >= 0) eventf("CD_INIT int2_ms %.1f", i2 * 1000.0 / CLK);
        break; }
    case 0x0C: case 0x0D: case 0x0B: cd_push(ack, 3, r, 1, K_RESP); break;         /* Demute / Setfilter / Mute */
    case 0x0E: {                                                                    /* Setmode */
        int s2 = (cd_param[0] >> 7) & 1;
        cd_mode = cd_param[0]; cd_dsize = (cd_mode & 0x20) ? 0x924 : 0x800;
        if (s2 != cd_2x) {
            u64 t0 = cyc > cd_speed_ready ? cyc : cd_speed_ready, d = o_speed_ms >= 0 ? (u64)(o_speed_ms * CLK / 1000) : (u64)((s2 ? 0.6 : 1.0) * CLK);
            cd_2x = s2; cd_speed_ready = t0 + d;
            eventf("CD_SPEED x%d ms %.1f", s2 ? 2 : 1, (cd_speed_ready - cyc) * 1000.0 / CLK);
        }
        cd_push(ack, 3, r, 1, K_RESP); break; }
    case 0x11: {                                                                    /* GetlocP */
        u32 pos = o_stat ? cd_head() : cd_playing ? cd_play_pos() : cd_cur, a = pos + 150, rel; int t = track_of(pos);
        rel = pos >= trk_lba[t] ? pos - trk_lba[t] : trk_lba[t] - pos;
        r[0] = i2bcd(t); r[1] = pos >= trk_lba[t] ? 1 : 0; r[2] = i2bcd(rel / 4500); r[3] = i2bcd((rel / 75) % 60); r[4] = i2bcd(rel % 75);
        r[5] = i2bcd(a / 4500); r[6] = i2bcd((a / 75) % 60); r[7] = i2bcd(a % 75);
        cd_push(ack, 3, r, 8, K_RESP); break; }
    case 0x13: r[1] = 1; r[2] = i2bcd(ntracks); cd_push(ack, 3, r, 3, K_RESP); break;
    case 0x14: {                                                                    /* GetTD */
        int t = bcd2i(cd_param[0]); u32 l;
        if (t == 0) l = (u32)cd_sectors; else if (t <= ntracks) l = trk_lba[t]; else { r[0] |= 1; r[1] = 0x10; cd_push(ack, 5, r, 2, K_RESP); break; }
        l += 150; r[1] = i2bcd(l / 4500); r[2] = i2bcd((l / 75) % 60); cd_push(ack, 3, r, 3, K_RESP); break; }
    case 0x15: case 0x16:                                                           /* SeekL / SeekP */
        if (o_stat || o_pausa) {              /* busqueda de verdad: INT3, INT2 al acabar; luego en pausa alli */
            u64 st, t0;
            cd_cur = cd_head(); st = seek_ticks(cd_cur, cd_loc); cd_seek_from = cd_cur;
            cd_stop_activity("seek");
            cd_push(ack, 3, r, 1, K_RESP);
            t0 = cyc + ack > cd_speed_ready ? cyc + ack : cd_speed_ready;
            cd_seek_ex = 1; cd_seek_t0 = t0; cd_seek_t1 = t0 + st; cd_seek_to = cd_cur = cd_loc;
            r[0] = cd_stat; cd_push(t0 + st - cyc, 2, r, 1, K_RESP);
            eventf("CD_SEEK lba %u ms %.1f", cd_loc, (t0 + st - cyc) * 1000.0 / CLK);
        } else cd_push(ack, 3, r, 1, K_RESP);
        break;
    case 0x1E: { u64 ia = o_int3 ? cd_ack(1) : ack; cd_push(ia, 3, r, 1, K_RESP); if (o_stat) r[0] = cd_stat; cd_push(ia + (u64)(CLK / 2), 2, r, 1, K_RESP); break; }  /* ReadTOC */
    case 0x19: r[0] = 0x20; cd_push(ack, 3, r, 1, K_RESP); break;
    default: cd_push(ack, 3, r, 1, K_RESP); break;
    }
    (void)n;
    cd_np = 0; cd_primera = 0;
}
static void cd_tick(void)
{
    int i;
    if (cd_old_n && cd_reading && cyc >= cd_old_t && cyc < cd_next_sector && (cd_if & 7) == 0 && cdqn < 15) {
        u8 r[1]; r[0] = cd_stat | 0x20;
        cd_read_sector(cd_old_lba);
        if (cd_dsize == 0x924) memcpy(cd_data, cd_sector_buf + 12, 0x924); else memcpy(cd_data, cd_sector_buf + 24, 2048);
        cd_dlen = cd_dsize; cd_dpos = cd_dsize;
        cd_push(0, 1, r, 1, K_SECTOR);
        eventf("CD_SECTOR_VIEJO lba %u", cd_old_lba);
        cd_old_lba++; cd_old_n--; cd_old_t = cyc + (cd_2x ? 225792 : 451584);
    }
    /* CD_ERR: uno de cada n sectores es un error (INT5 04h) y la unidad deja de leer */
    if (o_err && cd_reading && cyc >= cd_next_sector && ++cd_err_n % o_err == 0) {
        u8 r[2]; r[0] = cd_st() | 0x05; r[1] = 0x04;
        cd_reading = 0; cd_push(0, 5, r, 2, K_RESP);
        eventf("CD_ERR_LECTURA lba %u", cd_cur);
    }
    /* entregar el siguiente evento si la bandera esta libre */
    if (cd_reading && cyc >= cd_next_sector) {
        u8 r[1]; r[0] = cd_stat | 0x20;
        cd_read_sector(cd_cur);
        if ((cd_if & 7) == 0 && cdqn < 15) {
            /* 0x924: todo menos la sincronia (cabecera, subcabecera, datos, EDC/ECC); 0x800: solo los datos */
            if (cd_dsize == 0x924) memcpy(cd_data, cd_sector_buf + 12, 0x924); else memcpy(cd_data, cd_sector_buf + 24, 2048);
            cd_dlen = cd_dsize; cd_dpos = cd_dsize; /* hasta pedir BFRD */
            cd_push(0, 1, r, 1, K_SECTOR);
        } else eventf("CD_SECTOR_LOST lba %u", cd_cur);
        cd_cur++;
        cd_next_sector += cd_2x ? 225792 : 451584;
    }
    if (cd_playing && (cd_mode & 2)) {
        /* pausa automatica al final de la pista (antes del pregap de la siguiente): INT4 */
        u32 pos = cd_play_pos(), fin = cd_play_trk >= 1 ? trk_end[cd_play_trk] : 0;
        if (o_ap && cd_play_trk >= 1) {      /* CD_AP: en el INDEX 01 siguiente (1) o al final del disco (2) */
            if (!cd_ap_paso && cd_play_trk < ntracks && pos >= fin) { cd_ap_paso = 1; eventf("CDDA_PASA_PISTA %d lba %u", cd_play_trk + 1, pos); }
            if (o_ap == 2) fin = (u32)cd_sectors; else if (cd_play_trk < ntracks) fin = trk_lba[cd_play_trk + 1];
        }
        if (cd_play_trk >= 1 && pos >= fin) {
            u8 r[1]; r[0] = cd_stat;
            cd_cur = pos; cd_playing = 0;
            eventf("CDDA_END lba %u", pos);
            cd_push(0, 4, r, 1, K_RESP);
        }
    }
    if ((cd_if & 7) == 0 && cdqn) {
        int best = -1;
        for (i = 0; i < cdqn; i++) if (cdq[i].t <= cyc && (best < 0 || cdq[i].t < cdq[best].t)) best = i;
        if (best >= 0) {
            cdev_t e = cdq[best];
            memmove(&cdq[best], &cdq[best + 1], (cdqn - best - 1) * sizeof(cdev_t)); cdqn--;
            cd_if = (cd_if & ~7) | e.irq;
            memcpy(cd_resp, e.resp, e.n); cd_nresp = e.n; cd_rpos = 0;
            if (e.irq == 1) cd_sector_ready = 1;
            if (e.kind == K_ACK && cd_ocupado) cd_ocupado--;
            if ((o_glitch || o_glitch5) && (e.irq == 3 || e.irq == 5)) {   /* CD_GLITCH: la 1a lectura de HINTSTS a medias */
                if (e.irq == 3) cd_gl_n3++; else cd_gl_n5++;
                cd_gl_arm = 1; cd_gl_t = cyc;
            }
        }
    }
}
static u8 cd_rd(u32 r)
{
    switch (r) {
    case 0: {
        u8 s = cd_idx;
        if (cd_np == 0) s |= 0x08;
        if (cd_np < 16) s |= 0x10;
        if (cd_rpos < cd_nresp) s |= 0x20;
        if (cd_dpos < cd_dlen) s |= 0x40;
        if (cd_ocupado) s |= 0x80;            /* CD_BUSY: BUSYSTS */
        return s; }
    case 1: return cd_rpos < cd_nresp ? cd_resp[cd_rpos++] : 0;
    case 2: return cd_dpos < cd_dlen ? cd_data[cd_dpos++] : 0;
    case 3:
        if (!(cd_idx & 1)) return 0xE0 | cd_ie;
        if (cd_gl_arm) {                      /* CD_GLITCH: primera lectura; a medias solo si se sondeaba al cambiar */
            int t = cd_if & 7, g = 0;
            cd_gl_arm = 0;
            if (!o_glvent || cyc - cd_gl_t <= (u64)o_glvent) {
                if (t == 3 && o_glitch && ++cd_gl_e3 % o_glitch == 0) g = 1;
                if (t == 5 && o_glitch5 && ++cd_gl_e5 % o_glitch5 == 0) g = 4;
            }
            if (g) { cd_gl_hechos++; eventf("CD_GLITCH INT%d se lee %d (%llu ciclos tras el cambio)", t, g, (unsigned long long)(cyc - cd_gl_t)); return 0xE0 | (cd_if & ~7) | g; }
        }
        return 0xE0 | cd_if;
    }
    return 0;
}
static void cd_wr(u32 r, u8 v)
{
    if (r == 0) { cd_idx = v & 3; return; }
    switch (cd_idx * 4 + r) {
    case 1: cd_command(v); break;
    case 2: if (cd_np < 16) cd_param[cd_np++] = v; break;
    case 3: if (v & 0x80) { if (cd_sector_ready) { cd_dpos = 0; cd_sector_ready = 0; } } else { cd_dlen = 0; cd_dpos = 0; } break;
    case 6: cd_ie = v & 0x1F; break;
    case 7: if (v & 7) cd_gl_arm = 0; cd_if &= ~(v & 0x1F); if (v & 0x40) cd_np = 0; if (v & 7) { cd_nresp = 0; cd_rpos = 0; } break;
    default: break;
    }
}
static int load_cue(const char *cue)
{
    char line[512], bin[512] = "", dir[512]; FILE *f = fopen(cue, "r"); int t = 0; const char *sl;
    if (!f) return -1;
    strcpy(dir, cue); sl = strrchr(dir, '/'); if (sl) dir[sl - dir + 1] = 0; else dir[0] = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = strstr(line, "FILE \"");
        if (p) { char *q = strchr(p + 6, '"'); *q = 0; snprintf(bin, sizeof bin, "%s%s", dir, p + 6); }
        if (strstr(line, "TRACK")) sscanf(strstr(line, "TRACK") + 5, "%d", &t);
        if (strstr(line, "INDEX 01")) { int m, s, fr; sscanf(strstr(line, "INDEX 01") + 8, "%d:%d:%d", &m, &s, &fr); trk_lba[t] = (m * 60 + s) * 75 + fr; if (t > ntracks) ntracks = t; }
        if (strstr(line, "INDEX 00")) { int m, s, fr; sscanf(strstr(line, "INDEX 00") + 8, "%d:%d:%d", &m, &s, &fr); trk_idx0[t] = (m * 60 + s) * 75 + fr; }
    }
    fclose(f);
    cdf = fopen(bin, "rb"); if (!cdf) return -2;
    fseek(cdf, 0, SEEK_END); cd_sectors = ftell(cdf) / 2352;
    for (t = 1; t <= ntracks; t++) trk_end[t] = t < ntracks ? (trk_idx0[t + 1] ? trk_idx0[t + 1] : trk_lba[t + 1]) : (u32)cd_sectors;
    return 0;
}

/* ---------------------------------------------------------------- mando (SIO0) */
/* JOGCON=n: en el puerto 1 un JogCon (como el de DuckStation, src/core/jogcon.cpp): ID E3h (F3h en configuracion),
   43h entra/sale de la configuracion, 4Dh asigna el byte del motor, 42h da botones, posicion de la rueda (16 bits) y
   estado. Como el mando real, arranca en modo digital (41h, solo botones: JOG_YA=1, ya en modo JogCon) y pasa a modo
   JogCon con 44h 01 en configuracion (43h entra tambien desde digital), JOG_RETARDO cuadros despues (el mando real
   tarda). Suceso JOG_MODO. La rueda: izquierda/derecha
   de INPUTS la llevan a -n/+n (4 unidades por cuadro); soltada, el motor la vuelve a
   la posicion de sujecion (orden 3x tras Cx) o se queda donde este. Suceso JOG_MOTOR con cada orden distinta */
static int jog_n = -1, jog_cfg, jog_cmd, jog_pos, jog_hold, jog_sujeta, jog_orden = -1, jog_modo = -1, jog_pend = -1, jog_pend_v, jog_holg = -99999;
static u8 jog_map[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static void eventf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void jog_motor(u8 v)
{
    if (v != jog_orden) { jog_orden = v; eventf("JOG_MOTOR %02X pos %d", v, jog_pos); }
    if ((v >> 4) == 0xC) jog_hold = jog_pos;
    else if ((v >> 4) == 3) jog_sujeta = (v & 15) != 0;
    else if ((v >> 4) == 0) jog_sujeta = 0;
}
static void jog_cuadro(void)                    /* tras leer INPUTS: la rueda en vez de izquierda/derecha */
{
    int t, l = !(pad_buttons & 0x80), r = !(pad_buttons & 0x20);
    if (jog_pend > 0 && !--jog_pend) { jog_modo = jog_pend_v; eventf("JOG_MODO %d (con retardo)", jog_modo); }
    if (jog_n <= 0 || jog_modo != 1) return;
    pad_buttons |= 0xA0;
    if (jog_holg < -9999) jog_holg = getenv("JOG_HOLGURA") ? atoi(getenv("JOG_HOLGURA")) : 0;   /* el muelle la deja ahi */
    /* soltada: el muelle la lleva hacia la sujecion y para a JOG_HOLGURA de ella (sin pasarse: holgura del motor) */
    t = l ? -jog_n : r ? jog_n : !jog_sujeta ? jog_pos : jog_pos > jog_hold + jog_holg ? jog_hold + jog_holg
        : jog_pos < jog_hold - jog_holg ? jog_hold - jog_holg : jog_pos;
    jog_pos += t > jog_pos + 4 ? 4 : t < jog_pos - 4 ? -4 : t - jog_pos;
}
static int jog_byte(u8 v, int seq, u8 *resp)  /* devuelve ack */
{
    int k = seq - 3;
    if (jog_modo < 0) jog_modo = getenv("JOG_YA") != 0;
    if (seq == 1) { jog_cmd = v; *resp = jog_cfg ? 0xF3 : jog_modo ? 0xE3 : 0x41; return 1; }
    if (seq == 2) { *resp = 0x5A; return 1; }
    if (k < 0 || k > 5) { *resp = 0xFF; return 0; }
    *resp = 0;
    if (jog_cmd == 0x42 && !jog_modo && !jog_cfg) {     /* digital: dos bytes de botones */
        *resp = k == 0 ? (u8)pad_buttons : (u8)(pad_buttons >> 8);
        return k < 1;
    }
    if (jog_cmd == 0x42) {
        u8 d[6] = {(u8)pad_buttons, (u8)(pad_buttons >> 8), (u8)jog_pos, (u8)(jog_pos >> 8), 0, 0};
        *resp = d[k];
        if (jog_map[k] == 0) jog_motor(v);
    } else if (jog_cmd == 0x43) { if (k == 0) jog_cfg = v == 1; }
    else if (jog_cmd == 0x44 && jog_cfg) {
        if (k == 0 && (v == 0 || v == 1) && v != jog_modo) {
            int r = getenv("JOG_RETARDO") ? atoi(getenv("JOG_RETARDO")) : 0;
            if (r > 0) { jog_pend = r; jog_pend_v = v; } else { jog_modo = v; eventf("JOG_MODO %d", v); }
        }
    }
    else if (jog_cmd == 0x4D && jog_cfg) { *resp = jog_map[k]; jog_map[k] = v; }
    return k < 5;
}
/* NEGCON=n: en el puerto 1 un NeGcon (ID 23h): de INPUTS, izquierda/derecha lo retuercen n desde 80h (4 por cuadro;
   suelto vuelve al centro), X es I a fondo, Q es A, C es B, 1 es L a fondo y 2 es R */
static int neg_n = -1, neg_tw = 0x80;
static int neg_byte(int seq, u8 *resp)
{
    int k = seq - 3, l = !(pad_buttons & 0x80), r = !(pad_buttons & 0x20);
    if (seq == 1) {                                       /* cada sondeo: el giro */
        int t = 0x80 + (l ? -neg_n : r ? neg_n : 0);
        neg_tw += t > neg_tw + 4 ? 4 : t < neg_tw - 4 ? -4 : t - neg_tw;
        *resp = 0x23; return 1;
    }
    if (seq == 2) { *resp = 0x5A; return 1; }
    if (k < 0 || k > 5) { *resp = 0xFF; return 0; }
    {
        u16 b = 0xFFFF;
        if (!(pad_buttons & 0x08)) b &= ~0x08;             /* START */
        b &= (u16)(pad_buttons | ~0x50);                   /* arriba, abajo */
        if (!(pad_buttons & 0x8000)) b &= ~0x2000;         /* Q -> A */
        if (!(pad_buttons & 0x0001)) b &= ~0x1000;         /* C -> B */
        if (!(pad_buttons & 0x0800)) b &= ~0x0800;         /* 2 -> R */
        u8 d[6] = {(u8)b, (u8)(b >> 8), (u8)neg_tw, (u8)(!(pad_buttons & 0x4000) ? 0xFF : 0), 0, (u8)(!(pad_buttons & 0x0400) ? 0xFF : 0)};
        *resp = d[k];
    }
    return k < 5;
}
static void joy_data_write(u8 v)
{
    u8 resp = 0xFF; int ack = 0;
    if (!(joy_ctrl & 2)) { joy_seq = -1; }
    if (joy_seq <= 0) {
        if (v == 0x01) { resp = 0xFF; ack = 1; joy_seq = 1; } else { resp = 0xFF; joy_seq = -1; }
    } else if ((neg_n < 0 ? (neg_n = getenv("NEGCON") ? atoi(getenv("NEGCON")) : 0) : neg_n) > 0) {
        ack = neg_byte(joy_seq, &resp); joy_seq++;
    } else if ((jog_n < 0 ? (jog_n = getenv("JOGCON") ? atoi(getenv("JOGCON")) : 0) : jog_n) > 0) {
        ack = jog_byte(v, joy_seq, &resp); joy_seq++;
    } else {
        switch (joy_seq) {
        case 1: resp = 0x41; ack = 1; break;
        case 2: resp = 0x5A; ack = 1; break;
        case 3: resp = pad_buttons & 0xFF; ack = 1; break;
        case 4: resp = pad_buttons >> 8; ack = 0; break;
        default: resp = 0xFF; ack = 0; break;
        }
        joy_seq++;
    }
    joy_rx = resp; joy_rx_valid = 0;
    joy_rx_at = cyc + (u64)(joy_baud ? joy_baud : 0x88) * 8;
    joy_tx_busy_until = cyc + 16;
    joy_ack_at = ack ? joy_rx_at + 100 : 0;
}
static u16 joy_stat(void)
{
    u16 s = 0;
    /* bit0: listo para enviar (salvo mientras se transmite el byte), bit2: envio terminado, bit1: hay byte recibido */
    if (!joy_tx_busy_until || cyc >= joy_tx_busy_until) s |= 1 | 4;
    if (joy_rx_at != (u64)-1 && cyc >= joy_rx_at) s |= 2;
    if (joy_ack_at && cyc >= joy_ack_at && cyc < joy_ack_at + 200) s |= 0x80;
    return s;
}
/* ---------------------------------------------------------------- bus */
static u32 io_read(u32 a, int sz, int *cost)
{
    u32 o = a - 0x1F801000;
    *cost = IO_MISC;
    if (o >= 0x800 && o < 0x804) { *cost = IO_CD; return cd_rd(o & 3); }
    if (o == 0x810) { *cost = IO_GPU; return 0; }
    if (o == 0x814) { *cost = IO_GPU; return gpustat(); }
    if (o >= 0xC00 && o < 0xE00) {
        u32 r = (o - 0xC00) / 2;
        *cost = IO_SPU;
        if (o == 0xDAE) { spu_avanza(); return (spu[(0xDAA - 0xC00) / 2] & 0x3F) | (spu_irq ? 0x40 : 0); }   /* SPUSTAT: modo y bandera del IRQ */
        if (o < 0xD80 && (o & 0xE) == 0xC) { spu_avanza(); return voz_on[(o - 0xC00) / 16] ? 0x7FFF : 0; }   /* volumen ADSR: suena o no */
        if (o == 0xD9C) return spu_endx() & 0xFFFF;
        if (o == 0xD9E) return spu_endx() >> 16;
        if (o == 0xD88) return spu_kon & 0xFFFF;
        if (o == 0xD8C) return spu_koff & 0xFFFF;
        return spu[r];
    }
    switch (o) {
    case 0x070: return I_STAT;
    case 0x074: return I_MASK;
    case 0x040: { u8 v = cyc >= joy_rx_at ? joy_rx : 0xFF; joy_rx_at = cyc >= joy_rx_at ? (u64)-1 : joy_rx_at; return v; }
    case 0x044: return joy_stat();
    case 0x048: return joy_mode;
    case 0x04A: return joy_ctrl;
    case 0x04E: return joy_baud;
    case 0x0F0: return DPCR;
    case 0x0F4: return DICR;
    case 0x100: return (u32)(((cyc - t0_base)) & 0xFFFF);
    case 0x110: {
        u64 n = (u64)((cyc - t1_base) / LINE_CYC); u32 v = (u32)(n & 0xFFFF);
        /* T1_GLITCH: la lectura coincide con el cambio (menos de un ciclo) y hay acarreo: el bit mas alto que cambia
           aun no ha cambiado */
        if (o_t1g && n && (double)(cyc - t1_base) - n * LINE_CYC < 1.0 && (v & 1) == 0 && ++t1_lect % o_t1g == 0) {
            u32 c = (v ^ (v - 1)) & 0xFFFF, hb = 0x8000; while (!(c & hb)) hb >>= 1;
            t1_rotas++; eventf("T1_ROTO %04x se lee %04x", v, v ^ hb); v ^= hb;
        }
        return v; }
    case 0x114: return t1_mode;
    case 0x120: return (u32)(((cyc - t2_base) / ((t2_mode & 0x200) ? 8 : 1)) & 0xFFFF);
    }
    if (o >= 0x080 && o < 0x0F0) {
        int ch = (o - 0x080) / 16, r = (o & 15) / 4;
        if (r == 0) return dma_madr[ch];
        if (r == 1) return dma_bcr[ch];
        if (r == 2) { if ((dma_chcr[ch] & 0x01000000) && cyc >= dma_done[ch]) dma_chcr[ch] &= ~0x11000000; return dma_chcr[ch]; }
    }
    (void)sz;
    return 0;
}
static void io_write(u32 a, u32 v, int sz, int *cost)
{
    u32 o = a - 0x1F801000;
    *cost = IO_MISC;
    if (o >= 0x800 && o < 0x804) { *cost = IO_CD; cd_wr(o & 3, (u8)v); return; }
    if (o == 0x810) { *cost = IO_GPU; gp0_write(v); return; }
    if (o == 0x814) { *cost = IO_GPU; gp1_write(v); return; }
    if (o >= 0xC00 && o < 0xE00) {
        u32 r = (o - 0xC00) / 2;
        *cost = IO_SPU;
        wav_hasta(cyc);
        if (o >= 0xD88 && o <= 0xD8E && wavf) wav_teclas(o == 0xD88 ? v & 0xFFFF : o == 0xD8A ? (v & 0xFF) << 16 : 0,
                                                       o == 0xD8C ? v & 0xFFFF : o == 0xD8E ? (v & 0xFF) << 16 : 0);
        if (r < 0xC0 && (r & 7) == 7) spu_avanza();         /* direccion de repeticion: la voz ya avanzo hasta ahora */
        if (r < 0xC0 && (r & 7) == 2 && spu[r] != (u16)v) {  /* tono: hasta ahora con el de antes; con 0 la voz no avanza
                                                               (al volver, el bloque sigue donde se quedo) */
            int vz = (int)(r >> 3);
            spu_avanza();
            if (!spu[r] && v) voz_tblk[vz] += cyc - voz_p0[vz];
            else if (spu[r] && !v) voz_p0[vz] = cyc;
        }
        spu[r] = (u16)v;
        if (o == 0xDA6) spu_addr = (v & 0xFFFF) * 8;
        if (o == 0xDAA && !(v & 0x40)) spu_irq = 0;          /* SPUCNT sin el bit 6: se reconoce el IRQ */
        if (o == 0xD9C || o == 0xD9E) { if (endx_borra < 0) endx_borra = getenv("ENDX_BORRA") != 0; if (endx_borra) spu_endx_bits &= o == 0xD9C ? ~0xFFFFu : 0xFFFFu; }
        if (o >= 0xD88 && o <= 0xD8E) {   /* KON / KOFF: se sustituye la mitad del registro */
            u32 *rg = o < 0xD8C ? &spu_kon : &spu_koff, sh = (o & 2) ? 16 : 0, perd;
            int k;
            spu_avanza();
            perd = (*rg >> sh) & 0xFFFF & ~v;
            for (k = 0; k < 16; k++) if (perd & (1 << k)) { spu_perdidas++; eventf("SPU_PERDIDA %s voz %d", o < 0xD8C ? "KON" : "KOFF", (int)sh + k); }
            *rg = (*rg & ~(0xFFFFu << sh)) | ((v & 0xFFFF) << sh);
            for (k = 0; k < 16; k++) if (v & (1 << k)) eventf("SPU_%s voz %d addr %u", o < 0xD8C ? "KON" : "KOFF", (int)sh + k, spu[(sh + k) * 8 + 3] * 8);
        }
        return;
    }
    switch (o) {
    case 0x070: I_STAT &= v; return;
    case 0x074: I_MASK = v; return;
    case 0x040: joy_data_write((u8)v); return;
    case 0x048: joy_mode = v; return;
    case 0x04A: joy_ctrl = v; if (!(v & 2)) joy_seq = 0; if (v & 0x40) { joy_seq = 0; } return;
    case 0x04E: joy_baud = v; return;
    case 0x0F0: DPCR = v; return;
    case 0x0F4: DICR = v; return;
    case 0x100: t0_base = cyc; return;
    case 0x104: t0_mode = v; t0_base = cyc; return;
    case 0x110: t1_base = cyc; return;
    case 0x114: t1_mode = v; t1_base = cyc; return;
    case 0x120: t2_base = cyc; return;
    case 0x124: t2_mode = v; t2_base = cyc; return;
    }
    if (o >= 0x080 && o < 0x0F0) {
        int ch = (o - 0x080) / 16, r = (o & 15) / 4;
        if (r == 0) dma_madr[ch] = v & 0xFFFFFF;
        if (r == 1) dma_bcr[ch] = v;
        if (r == 2) { dma_chcr[ch] = v; if (v & 0x01000000) dma_start(ch); }
    }
    (void)sz;
}
static u32 mem_read(u32 a, int sz)
{
    u32 p = a & 0x1FFFFFFF;
    int c = 0; u32 v;
    if (a == 0xFFFE0130) return cache_ctrl;
    if (o_cola && ((a >> 29) == 5 || p == 0x1F801800)) cola_t = 0;   /* KSEG1, o leer lo escrito: se vacia la cola */
    if (p < 0x800000) {
        p &= 0x1FFFFF;
        stall_cyc += RAM_LD;
        if (sz == 1) return ram[p];
        if (sz == 2) return ram[p] | (ram[p + 1] << 8);
        return ram[p] | (ram[p + 1] << 8) | (ram[p + 2] << 16) | ((u32)ram[p + 3] << 24);
    }
    if (p >= 0x1F800000 && p < 0x1F800400) {
        p &= 0x3FF;
        if (sz == 1) return scratch[p];
        if (sz == 2) return scratch[p] | (scratch[p + 1] << 8);
        return scratch[p] | (scratch[p + 1] << 8) | (scratch[p + 2] << 16) | ((u32)scratch[p + 3] << 24);
    }
    if (p >= 0x1F801000 && p < 0x1F803000) {
        u32 al = p & ~3;
        if (cola_t && p > 0x1F801800 && p < 0x1F801804 && cyc + stall_cyc < cola_t && cola_idx != cd_idx) {
            /* CD_COLA: la escritura del banco aun esta en la cola y la lectura sale antes: banco anterior */
            u8 ix = cd_idx, bueno = (ix & 1) ? 0xE0 | cd_if : 0xE0 | cd_ie;
            cd_idx = cola_idx; v = io_read(p, sz, &c); cd_idx = ix; cola_n++;
            if (p == 0x1F801803 && (v & 0xFF) != bueno) eventf("CD_COLA 1F801803 en el banco %d (no %d): %02x en vez de %02x", cola_idx, ix, v & 0xFF, bueno);
        } else
        v = io_read(sz == 1 ? p : (sz == 2 ? p & ~1 : al), sz, &c);
        stall_cyc += c;
        if (sz == 1 && !(p >= 0x1F801800 && p < 0x1F801804) && p != 0x1F801040) v >>= (p & 3) * 8;
        return sz == 1 ? v & 0xFF : sz == 2 ? v & 0xFFFF : v;
    }
    return 0;
}
static void mem_write(u32 a, u32 v, int sz)
{
    u32 p = a & 0x1FFFFFFF;
    int c = 0;
    if (a == 0xFFFE0130) { cache_ctrl = v; return; }
    if (SR & 0x10000) return;   /* cache aislada */
    if (o_cola && (a >> 29) == 5) cola_t = 0;
    if (p < 0x800000) {
        p &= 0x1FFFFF;
        ram[p] = v; if (sz >= 2) ram[p + 1] = v >> 8; if (sz == 4) { ram[p + 2] = v >> 16; ram[p + 3] = v >> 24; }
        return;
    }
    if (p >= 0x1F800000 && p < 0x1F800400) {
        p &= 0x3FF;
        scratch[p] = v; if (sz >= 2) scratch[p + 1] = v >> 8; if (sz == 4) { scratch[p + 2] = v >> 16; scratch[p + 3] = v >> 24; }
        return;
    }
    if (p >= 0x1F801000 && p < 0x1F803000) {
        u8 ix = cd_idx;
        io_write(p, v, sz, &c); stall_cyc += c;
        /* CD_COLA: banco escrito por KUSEG/KSEG0: sigue en la cola hasta o_cola ciclos despues de la instruccion */
        if (o_cola && p == 0x1F801800 && (a >> 29) != 5) { cola_idx = ix; cola_t = cyc + 1 + stall_cyc + o_cola; }
    }
}

/* ---------------------------------------------------------------- CPU */
static u32 fetch(u32 a)
{
    u32 p = a & 0x1FFFFF;
    if ((a >> 29) == 0 || (a >> 29) == 4) {
        u32 line = (a >> 4) & 255, tag = a & 0x1FFFF000 & ~0xFFF, w = (a >> 2) & 3;
        tag = (a & 0x1FFFFFF0) >> 12;
        if (icache_tag[line] != tag || !(icache_valid[line] & (1u << w))) {
            int n = 4 - w;
            stall_cyc += ICF_BASE + ICF_WORD * n;
            if (icache_tag[line] != tag) icache_valid[line] = 0;
            icache_tag[line] = tag;
            icache_valid[line] |= (0xF << w) & 0xF;
        }
    } else stall_cyc += RAM_LD;
    return ram[p] | (ram[p + 1] << 8) | (ram[p + 2] << 16) | ((u32)ram[p + 3] << 24);
}
#define RS ((op >> 21) & 31)
#define RT ((op >> 16) & 31)
#define RD ((op >> 11) & 31)
#define IMM ((s32)(s16)op)
#define IMMU (op & 0xFFFF)
static int wr_reg;
static void setr(int r, u32 v) { if (r) { R[r] = v; wr_reg = r; } }
static void load(int r, u32 v) { if (r) { ld_r = r; ld_v = v; } }
static void branch(u32 t) { NPC = t; }
static void muldiv_wait(void) { if (muldiv_ready > cyc + stall_cyc) stall_cyc = muldiv_ready - cyc; }
static void exception(int code)
{
    fprintf(stderr, "excepcion %d en %08X (ciclo %llu, cuadro %ld)\n", code, PC - 4, (unsigned long long)cyc, game_frame);
    if (code == 9) fprintf(stderr, "  break %x\n", (fetch(PC - 4) >> 6) & 0xFFFFF);
    halted = 1;
}
/* ---- LOADER_IRQ: el EXE empieza con las interrupciones activadas (LoadExec del menu de PSIO) y psxprof las entrega:
   el vector 80000080h salta a la rutina del menu (en lo que es la BSS del EXE: I_STAT = 0, jr EPC, rfe; sin ceros) */
static u32 irq_h, irq_b0, irq_b1, sym_main; static long irq_n; static int irq_en, irq_main;
static void irq_init(u32 b0, u32 b1)
{
    static const u32 h[5] = { 0x401A7000, 0x3C1B1F80, 0xAF601070, 0x03400008, 0x42000010 };
    const char *e = getenv("LOADER_IRQ_PC"); int k;
    irq_b0 = b0; irq_b1 = b1; irq_h = e ? (u32)strtoul(e, 0, 16) : b0 + 0x40;
    for (k = 0; k < 5; k++) wr32ram(irq_h + k * 4, h[k]);
    wr32ram(0x80000080, 0x08000000 | ((irq_h >> 2) & 0x3FFFFFF)); wr32ram(0x80000084, 0);
    SR = 0x401; I_MASK = 1;
    fprintf(stderr, "LOADER_IRQ: SR=401h I_MASK=1, rutina del menu en %08X (BSS %08X-%08X)\n", irq_h, b0, b1);
}
static void irq_check(void)
{
    if (!irq_main && PC == sym_main) { irq_main = 1; eventf("MAIN sr %x i_mask %x interrupciones_antes %ld", SR, I_MASK, irq_n); }
    if (irq_en && PC == EPC) irq_en = 0;                       /* volvio de la interrupcion */
    if (irq_en && PC >= irq_b0 && PC < irq_b1 && !rd32ram(PC)) {
        eventf("IRQ_CUELGUE pc %08x epc %08x", PC, EPC);
        fprintf(stderr, "LOADER_IRQ: la interrupcion %ld salta a la rutina %08X, ya borrada por crt0 (se ejecutan ceros en %08X; EPC %08X, ciclo %llu): cuelgue antes de main\n",
                irq_n, irq_h, PC, EPC, (unsigned long long)cyc);
        halted = 1; return;
    }
    if ((SR & 0x401) == 0x401 && (I_STAT & I_MASK) && NPC == PC + 4) {   /* no en el hueco de un salto tomado */
        irq_n++; irq_en = 1;
        eventf("IRQ_CARGADOR %ld epc %08x i_stat %x rutina %s", irq_n, PC, I_STAT, rd32ram(irq_h) ? "intacta" : "borrada");
        EPC = PC; CAUSE = (CAUSE & ~0x7C) | 0x400; SR = (SR & ~0x3F) | ((SR << 2) & 0x3C);
        PC = 0x80000080; NPC = PC + 4;
    }
}
static void step(void)
{
    u32 pc = PC, op, s, t;
    int pend_r = ld_r; u32 pend_v = ld_v;
    stall_cyc = 0;
    op = fetch(pc);
    PC = NPC; NPC += 4;
    ld_r = 0; wr_reg = 0;
    s = R[RS]; t = R[RT];
    switch (op >> 26) {
    case 0x00:
        switch (op & 63) {
        case 0x00: setr(RD, t << ((op >> 6) & 31)); break;
        case 0x02: setr(RD, t >> ((op >> 6) & 31)); break;
        case 0x03: setr(RD, (u32)((s32)t >> ((op >> 6) & 31))); break;
        case 0x04: setr(RD, t << (s & 31)); break;
        case 0x06: setr(RD, t >> (s & 31)); break;
        case 0x07: setr(RD, (u32)((s32)t >> (s & 31))); break;
        case 0x08: branch(s); break;
        case 0x09: setr(RD, pc + 8); branch(s); break;
        case 0x0C: exception(8); break;
        case 0x0D: exception(9); break;
        case 0x10: muldiv_wait(); setr(RD, HI); break;
        case 0x11: HI = s; break;
        case 0x12: muldiv_wait(); setr(RD, LO); break;
        case 0x13: LO = s; break;
        case 0x18: { s64 r = (s64)(s32)s * (s32)t; LO = (u32)r; HI = (u32)(r >> 32);
            u32 a = (s32)s < 0 ? ~s : s; muldiv_ready = cyc + (a < 0x800 ? 6 : a < 0x100000 ? 9 : 13); break; }
        case 0x19: { u64 r = (u64)s * t; LO = (u32)r; HI = (u32)(r >> 32);
            muldiv_ready = cyc + (s < 0x800 ? 6 : s < 0x100000 ? 9 : 13); break; }
        case 0x1A: if (t == 0) { LO = (s32)s < 0 ? 1 : 0xFFFFFFFF; HI = s; } else if (s == 0x80000000 && t == 0xFFFFFFFF) { LO = s; HI = 0; } else { LO = (u32)((s32)s / (s32)t); HI = (u32)((s32)s % (s32)t); }
            muldiv_ready = cyc + 36; break;
        case 0x1B: if (t == 0) { LO = 0xFFFFFFFF; HI = s; } else { LO = s / t; HI = s % t; } muldiv_ready = cyc + 36; break;
        case 0x20: case 0x21: setr(RD, s + t); break;
        case 0x22: case 0x23: setr(RD, s - t); break;
        case 0x24: setr(RD, s & t); break;
        case 0x25: setr(RD, s | t); break;
        case 0x26: setr(RD, s ^ t); break;
        case 0x27: setr(RD, ~(s | t)); break;
        case 0x2A: setr(RD, (s32)s < (s32)t); break;
        case 0x2B: setr(RD, s < t); break;
        default: exception(10); break;
        }
        break;
    case 0x01: {
        int rt = RT, cond = (rt & 1) ? (s32)s >= 0 : (s32)s < 0;
        if ((rt & 0x1E) == 0x10) setr(31, pc + 8);
        if (cond) branch(pc + 4 + (IMM << 2));
        break; }
    case 0x02: branch((pc & 0xF0000000) | ((op & 0x3FFFFFF) << 2)); break;
    case 0x03: setr(31, pc + 8); branch((pc & 0xF0000000) | ((op & 0x3FFFFFF) << 2)); break;
    case 0x04: if (s == t) branch(pc + 4 + (IMM << 2)); break;
    case 0x05: if (s != t) branch(pc + 4 + (IMM << 2)); break;
    case 0x06: if ((s32)s <= 0) branch(pc + 4 + (IMM << 2)); break;
    case 0x07: if ((s32)s > 0) branch(pc + 4 + (IMM << 2)); break;
    case 0x08: case 0x09: setr(RT, s + IMM); break;
    case 0x0A: setr(RT, (s32)s < IMM); break;
    case 0x0B: setr(RT, s < (u32)IMM); break;
    case 0x0C: setr(RT, s & IMMU); break;
    case 0x0D: setr(RT, s | IMMU); break;
    case 0x0E: setr(RT, s ^ IMMU); break;
    case 0x0F: setr(RT, IMMU << 16); break;
    case 0x10: {
        u32 rs = RS;
        if (rs == 0) { u32 v = 0; int rd = RD; if (rd == 12) v = SR; else if (rd == 13) v = CAUSE; else if (rd == 14) v = EPC; else if (rd == 15) v = 2; load(RT, v); }
        else if (rs == 4) { int rd = RD; if (rd == 12) SR = t; else if (rd == 13) CAUSE = t; }
        else if (rs == 16 && (op & 63) == 0x10) SR = (SR & ~15) | ((SR >> 2) & 15);
        break; }
    case 0x20: load(RT, (u32)(s32)(s8)mem_read(s + IMM, 1)); break;
    case 0x21: { u32 a = s + IMM; if (a & 1) { exception(4); break; } load(RT, (u32)(s32)(s16)mem_read(a, 2)); break; }
    case 0x22: { u32 a = s + IMM, w = mem_read(a & ~3, 4), cur = (pend_r == (int)RT) ? pend_v : t, sh = a & 3;
        u32 v = (cur & (0x00FFFFFF >> (sh * 8))) | (w << (24 - sh * 8)); load(RT, v); break; }
    case 0x23: { u32 a = s + IMM; if (a & 3) { exception(4); break; } load(RT, mem_read(a, 4)); break; }
    case 0x24: load(RT, mem_read(s + IMM, 1) & 0xFF); break;
    case 0x25: { u32 a = s + IMM; if (a & 1) { exception(4); break; } load(RT, mem_read(a, 2) & 0xFFFF); break; }
    case 0x26: { u32 a = s + IMM, w = mem_read(a & ~3, 4), cur = (pend_r == (int)RT) ? pend_v : t, sh = a & 3;
        u32 v = (cur & (0xFFFFFF00u << ((3 - sh) * 8))) | (w >> (sh * 8)); load(RT, v); break; }
    case 0x28: mem_write(s + IMM, t, 1); break;
    case 0x29: { u32 a = s + IMM; if (a & 1) { exception(5); break; } mem_write(a, t, 2); break; }
    case 0x2A: { u32 a = s + IMM, w = mem_read(a & ~3, 4), sh = a & 3; stall_cyc -= RAM_LD;
        w = (w & (0xFFFFFF00u << (sh * 8))) | (t >> (24 - sh * 8)); mem_write(a & ~3, w, 4); break; }
    case 0x2B: { u32 a = s + IMM; if (a & 3) { exception(5); break; } mem_write(a, t, 4); break; }
    case 0x2E: { u32 a = s + IMM, w = mem_read(a & ~3, 4), sh = a & 3; stall_cyc -= RAM_LD;
        w = (w & (0x00FFFFFF >> ((3 - sh) * 8))) | (t << (sh * 8)); mem_write(a & ~3, w, 4); break; }
    case 0x12: case 0x32: case 0x3A: break;   /* GTE: no se usa */
    default: exception(10); break;
    }
    if (pend_r && pend_r != wr_reg) R[pend_r] = pend_v;
    R[0] = 0;
    {
        u64 c = 1 + stall_cyc;
        cyc += c;
        if (prof_on && cyc >= prof_from_cyc && (pc & 0x1FFFFFFF) < 0x200000) { prof[(pc & 0x1FFFFF) >> 2] += c; prof_n[(pc & 0x1FFFFF) >> 2]++; }
    }
}

/* ---------------------------------------------------------------- simbolos */
static u32 sym_lookup(const char *mapfile, const char *name)
{
    char line[512]; FILE *f = fopen(mapfile, "r"); u32 v = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long long a; char ty, nm[400];   /* %llx: el nm de PSn00bSDK (Windows) da las direcciones con 64 bits */
        if (sscanf(line, "%llx %c %399s", &a, &ty, nm) == 3 && !strcmp(nm, name)) { v = (u32)a; break; }
    }
    fclose(f);
    return v;
}

int main(int argc, char **argv)
{
    FILE *f; u8 hdr[0x800]; u32 pc0, taddr, tsize, sp;
    long maxcyc_frames;
    u64 next_cd = 0;
    int was_loading = 0;
    const char *nmfile;
    if (argc < 5) { fprintf(stderr, "uso: psxprof imagen.cue exe nm.txt cuadros [dir]\n"); return 1; }
    if (load_cue(argv[1])) { fprintf(stderr, "no se pudo abrir %s\n", argv[1]); return 1; }
    f = fopen(argv[2], "rb"); if (!f) return 1;
    if (fread(hdr, 1, 0x800, f) != 0x800) return 1;
    pc0 = hdr[0x10] | hdr[0x11] << 8 | hdr[0x12] << 16 | (u32)hdr[0x13] << 24;
    taddr = hdr[0x18] | hdr[0x19] << 8 | hdr[0x1A] << 16 | (u32)hdr[0x1B] << 24;
    tsize = hdr[0x1C] | hdr[0x1D] << 8 | hdr[0x1E] << 16 | (u32)hdr[0x1F] << 24;
    if (fread(ram + (taddr & 0x1FFFFF), 1, tsize, f) != tsize) { fprintf(stderr, "exe corto\n"); return 1; }
    fclose(f);
    nmfile = argv[3];
    max_game_frames = atol(argv[4]);
    if (argc > 5) outdir = argv[5];
    if (getenv("RAM_LD")) RAM_LD = atoi(getenv("RAM_LD"));
    if (getenv("ICF_BASE")) ICF_BASE = atoi(getenv("ICF_BASE"));
    if (getenv("ICF_WORD")) ICF_WORD = atoi(getenv("ICF_WORD"));
    if (getenv("SHOT")) shot_every = atol(getenv("SHOT"));
    if (getenv("PROF_FROM")) prof_from_frame = atol(getenv("PROF_FROM"));
    {   /* modelos del hardware real (perfil ODE=... para lo que no se ponga a mano) */
        const char *od = getenv("ODE");
        if (od && !strcmp(od, "xstation")) ode_pre = "CD_STAT=1 CD_PAUSA_REAL=1 CD_SEEK_MUL=1.3 CD_SEEK_JIT=80 CD_INT3_RND=1 CD_BUSY=1 CD_GLITCH=1000 CD_GLITCH5=100";
        else if (od && !strcmp(od, "psio")) ode_pre = "CD_STAT=1 CD_SEEK_MUL=0.05 CD_SPEED_MS=0 CD_INIT_MS=5 CD_AP=1 CD_BUSY=1";
        else if (od) { fprintf(stderr, "ODE=%s desconocido (xstation o psio)\n", od); return 1; }
        o_stat = cfgf("CD_STAT", 0); o_pausa = cfgf("CD_PAUSA_REAL", 0); o_int3 = cfgf("CD_INT3_RND", 0); o_busy = cfgf("CD_BUSY", 0);
        o_seek_mul = cfgf("CD_SEEK_MUL", 1); o_seek_jit = cfgf("CD_SEEK_JIT", 0); o_speed_ms = cfgf("CD_SPEED_MS", -1);
        o_glitch = cfgf("CD_GLITCH", 0); o_glitch5 = cfgf("CD_GLITCH5", o_glitch ? (o_glitch / 10 > 0 ? o_glitch / 10 : 1) : 0);
        o_glvent = (long)cfgf("CD_GLITCH_VENT", 2048);
        o_err = cfgf("CD_ERR", 0); o_err80 = cfgf("CD_ERR80", 0); o_ap = cfgf("CD_AP", 0); o_init_ms = cfgf("CD_INIT_MS", -1);
        o_cola = cfgf("CD_COLA", 0); if (o_cola == 1) o_cola = 8;
        o_t1g = cfgf("T1_GLITCH", 0); o_irq = (long)cfgf("LOADER_IRQ", 0);
        if (cfg("CD_SEED")) { o_rng ^= (u32)cfgf("CD_SEED", 0) * 0x9E3779B9u; if (!o_rng) o_rng = 1; }   /* cada n su semilla (n | 1 juntaba 2k y 2k+1) */
        if (getenv("LINE_CYC")) LINE_CYC = atof(getenv("LINE_CYC"));
        if (od) fprintf(stderr, "ODE=%s: %s\n", od, ode_pre);
    }
    sym_vblank = sym_lookup(nmfile, "rc_vblank");
    if (!sym_vblank) { fprintf(stderr, "sin rc_vblank en %s\n", nmfile); return 1; }
    sym_ram = sym_lookup(nmfile, "rc_ram"); sym_vram = sym_lookup(nmfile, "rc_vram"); sym_pal = sym_lookup(nmfile, "rc_pal"); sym_spr = sym_lookup(nmfile, "rc_spr"); sym_vregs = sym_lookup(nmfile, "rc_vregs");
    sym_main = sym_lookup(nmfile, "main");
    sym_xram = sym_lookup(nmfile, "rc_xram"); sym_mcu = sym_lookup(nmfile, "mcu_cuadro"); sym_rcr = sym_lookup(nmfile, "rc_r"); sym_rcdl = sym_lookup(nmfile, "rc_dl");
    if (getenv("HUELLA")) huellaf = fopen(getenv("HUELLA"), "w");
    { char fn[600]; snprintf(fn, sizeof fn, "%s/cpu.csv", outdir); cpuf = fopen(fn, "w"); if (cpuf) fprintf(cpuf, "llamada,cyc_68k,cyc_vblank_ant,cyc_mcu_ant,modo\n"); }
    sym_tslot = sym_lookup(nmfile, "tslot"); sym_loading = sym_lookup(nmfile, "loading"); sym_in_race = sym_lookup(nmfile, "in_race");
    if (getenv("WAV") && (wavf = fopen(getenv("WAV"), "wb"))) { static const char z[44] = {0}; fwrite(z, 1, 44, wavf); }
    if (getenv("MISSLOG")) { char fn[600]; snprintf(fn, sizeof fn, "%s/falta.txt", outdir); misslog = fopen(fn, "w"); }
    dump_init();
    prof = calloc(0x80000, 8); prof_n = calloc(0x80000, 8);
    { char fn[600]; snprintf(fn, sizeof fn, "%s/eventos.txt", outdir); evf = fopen(fn, "w");
      snprintf(fn, sizeof fn, "%s/cuadros.csv", outdir); framef = fopen(fn, "w"); }
    /* imagen: huella de lo que se ve al empezar el cuadro; falta_capa/falta_spr: tiles visibles que no estaban en la
       cache al mandar la ultima lista; carga: hubo pantalla de carga (variable loading del port) desde el anterior;
       carrera: variable in_race del port */
    if (framef) fprintf(framef, "cuadro_juego,ciclo,vblank,ciclos_desde_anterior,gpu_ticks,estado,lista,gpu_fin,imagen,falta_capa,falta_spr,carga,carrera\n");
    sp = 0x801FFFF0;
    if (getenv("LOADER_SP")) sp = (u32)strtoul(getenv("LOADER_SP"), 0, 16);   /* $sp que deja el cargador */
    R[29] = sp; R[30] = sp;
    PC = pc0; NPC = pc0 + 4;
    SR = 0;
    frame_cyc = (u64)(LINE_CYC * LINES);
    next_vbl = frame_cyc;
    if (o_irq) {   /* BSS del EXE: de __bss_start a __bss_end (o lo que sigue al EXE) */
        u32 b0 = sym_lookup(nmfile, "__bss_start"), b1 = sym_lookup(nmfile, "__bss_end");
        if (!b0) b0 = taddr + tsize;
        if (b1 <= b0) b1 = b0 + 0x1000;
        irq_init(b0, b1);
        if (o_irq > 1) next_vbl = o_irq;
    }
    joy_rx_at = (u64)-1;
    maxcyc_frames = 0; (void)maxcyc_frames;
    while (!halted) {
        if ((PC & 0x1FFFFFFF) == (sym_vblank & 0x1FFFFFFF) && PC == sym_vblank) {
            u64 d = cyc - last_vblank_call;
            if (framef) fprintf(framef, "%ld,%llu,%llu,%llu,%llu,%08x,%llu,%llu,%08x,%d,%d,%d,%d\n", game_frame, (unsigned long long)cyc, (unsigned long long)vbl_count, (unsigned long long)d, (unsigned long long)gpu_ticks_total, state_hash(),
                                (unsigned long long)ll_start, (unsigned long long)ll_end, disp_hash(), miss_lay, miss_spr, was_loading, sym_in_race ? (int)rd32ram(sym_in_race) : 0);
            {   /* WATCH=var1,var2: cambios de esas variables del port (32 bits) en falta.txt */
                static int nw = -1; static u32 wa[8], wv[8]; static char wn[8][64]; int k;
                if (nw < 0) { const char *q = getenv("WATCH"); nw = 0;
                    while (q && *q && nw < 8) { k = 0; while (*q && *q != ',' && k < 63) wn[nw][k++] = *q++; wn[nw][k] = 0; if (*q == ',') q++;
                        {   char *pl = strchr(wn[nw], '+'); if (pl) *pl = 0;
                            wa[nw] = sym_lookup(nmfile, wn[nw]); if (pl) { *pl = '+'; if (wa[nw]) wa[nw] += strtol(pl + 1, 0, 0); } }
                        wv[nw] = 0xDEADBEEF; nw++; } }
                for (k = 0; k < nw; k++) if (wa[k] && misslog) { u32 v = rd32ram(wa[k]); if (v != wv[k]) { wv[k] = v; fprintf(misslog, "%ld W %s %d\n", game_frame, wn[k], (int)v); } }
            }
            {   /* POKE68=dir:valor[:desde],...: palabras del 68000 fijas (escenarios: reloj de la carrera parado) */
                const char *q = getenv("POKE68");
                while (q && *q && sym_ram) {
                    long ad = strtol(q, (char **)&q, 16), v = 0, d = 0;
                    if (*q == ':') v = strtol(q + 1, (char **)&q, 16);
                    if (*q == ':') d = strtol(q + 1, (char **)&q, 10);
                    if (game_frame >= d) { u32 o = (sym_ram & 0x1FFFFF) + (ad & 0x3FFE); ram[o] = v; ram[o + 1] = v >> 8; }
                    if (*q == ',') q++; else break;
                }
            }
            was_loading = 0;
            last_vblank_call = cyc;
            game_frame++;
            dump_entry();
            if (huellaf) fprintf(huellaf, "%ld %08x\n", game_frame, state_hash());
            if (getenv("ROMCHECK") && atol(getenv("ROMCHECK")) == game_frame) {   /* rc_romtab contra work/prog.bin (bytes cambiados de dos en dos) */
                static u8 *pr; u32 tab = sym_lookup(nmfile, "rc_romtab"), b, k, malos = 0;
                if (!pr) { FILE *f = fopen("../work/prog.bin", "rb"); pr = malloc(0x100000); if (f) { fread(pr, 1, 0x100000, f); fclose(f); } }
                for (b = 0; b < 4096 && tab; b++) {
                    u32 p = rd32ram(tab + b * 4);
                    if (!p) continue;
                    for (k = 0; k < 260; k++) {               /* y los 4 de la cola (lecturas que cruzan el bloque) */
                        u32 a = b * 256 + k; u8 v = ram[(p + a) & 0x1FFFFF];
                        if (v != pr[a ^ 1]) { if (malos++ < 20) fprintf(stderr, "ROMCHECK bloque %05X: %05X da %02X y es %02X\n", b << 8, a, v, pr[a ^ 1]); break; }
                    }
                }
                fprintf(stderr, "ROMCHECK cuadro %ld: %u bloques mal\n", game_frame, malos);
                {   /* rc_romres entero contra la ROM (lo que leen directo las lecturas a direccion fija) */
                    u32 tt = sym_lookup(nmfile, "rc_romres_tab"), tn = sym_lookup(nmfile, "rc_romres_n"), rr = sym_lookup(nmfile, "rc_romres"), n, o = 0, mal = 0;
                    if (tt && tn && rr) {
                        n = rd32ram(tn);
                        for (k = 0; k < n; k++) {
                            u32 a0 = rd32ram(tt + k * 8), a1 = rd32ram(tt + k * 8 + 4), i;
                            for (i = 0; i < a1 - a0 + 4; i++) if (ram[(rr + o + i) & 0x1FFFFF] != pr[(a0 + i) ^ 1]) { if (mal++ < 10) fprintf(stderr, "ROMRES tramo %05X-%05X: %05X mal\n", a0, a1, a0 + i); }
                            o += a1 - a0 + 4;
                        }
                        fprintf(stderr, "ROMRES: %u tramos, %u B, %u mal\n", n, o, mal);
                    }
                }
            }
            if (getenv("ESTADOAT") && atol(getenv("ESTADOAT")) == game_frame && sym_ram) {   /* rc_ram, rc_vram, rc_pal y rc_xram de esa llamada */
                char fn[600]; FILE *f; snprintf(fn, sizeof fn, "%s/estado.bin", outdir);
                if ((f = fopen(fn, "wb"))) { fwrite(ram + (sym_ram & 0x1FFFFF), 1, 0x10000, f); fwrite(ram + (sym_vram & 0x1FFFFF), 1, 0x10000, f); fwrite(ram + (sym_pal & 0x1FFFFF), 1, 0x2000, f); fwrite(ram + (sym_xram & 0x1FFFFF), 1, 0x2000, f); fclose(f); }
            }
            if (cpuf) fprintf(cpuf, "%ld,%llu,%llu,%llu,%u\n", game_frame, (unsigned long long)(cyc - t_vbout), (unsigned long long)vb_cyc, (unsigned long long)mcu_cyc, sym_ram ? rd16ram(sym_ram + 0xC298) : 0);
            t_vbin = cyc; ra_vb = R[31]; vb_cyc = mcu_cyc = 0;
            if (game_frame == prof_from_frame) { memset(prof, 0, 0x80000 * 8); memset(prof_n, 0, 0x80000 * 8); prof_from_cyc = cyc; }
            {   /* psxprof2: cuadros del 68000 como el banco (los vblanks pasados con la IRQ enmascarada cuentan: ~20 en el
                   arranque), con el contador de ciclos del recompilado (rc_r.cyrel + rc_dl = ciclos desde el vblank) */
                long rel = sym_rcr ? (long)(s32)rd32ram(sym_rcr + 100) + (long)(s32)rd32ram(sym_rcdl) : 0;
                frame68 += 1 + (rel > 0 ? rel / 219966 : 0);
            }
            pad_buttons = inputs_for(sym_rcr ? frame68 : game_frame); jog_cuadro();
            if ((shot_every && game_frame % shot_every == 0) || shot_at(game_frame)) { char nm[64]; snprintf(nm, sizeof nm, "shot_%05ld", game_frame); screenshot(nm); }
            if (game_frame >= max_game_frames) break;
        }
        if (ra_vb && PC == ra_vb) { vb_cyc = cyc - t_vbin; t_vbout = cyc; ra_vb = 0; }
        if (sym_mcu && PC == sym_mcu) { t_min = cyc; ra_mcu = R[31]; }
        else if (ra_mcu && PC == ra_mcu) { mcu_cyc += cyc - t_min; ra_mcu = 0; }
        step();
        if ((R[29] & 0x1FFFFF) < minsp && (R[29] >> 24) == 0x80) { minsp = R[29] & 0x1FFFFF; minsp_fr = game_frame; }
        else if ((R[29] >> 10) == (0x1F800000u >> 10) && (R[29] & 0x3FF) < minsp_spad) { minsp_spad = R[29] & 0x3FF; minsp_spad_fr = game_frame; minsp_spad_pc = PC; }
        while (cyc >= next_vbl) {
            I_STAT |= 1; vbl_set_cyc = next_vbl; next_vbl += frame_cyc; vbl_count++;
            if (getenv("INPUTSV")) pad_buttons = inputs_spec(getenv("INPUTSV"), vbl_count, inputs_for(game_frame));   /* por vblank (pausa) */
            {   /* SHOTV=v1,v2,...: captura de lo que se ve en esos vblanks (p. ej. la pantalla de carga del arranque) */
                const char *q = getenv("SHOTV");
                while (q && *q) { long v = strtol(q, (char **)&q, 10); if (v == (long)vbl_count) { char nm[64]; snprintf(nm, sizeof nm, "vbl_%05ld", v); screenshot(nm); } if (*q == ',') q++; else break; }
            }
        }
        if (cyc >= next_cd) { cd_tick(); next_cd = cyc + 64; if (sym_loading && rd32ram(sym_loading)) was_loading = 1; }
        if (o_irq) irq_check();
    }
    {
        char fn[600];
        snprintf(fn, sizeof fn, "%s/prof.bin", outdir); f = fopen(fn, "wb"); fwrite(prof, 8, 0x80000, f); fclose(f);
        snprintf(fn, sizeof fn, "%s/profn.bin", outdir); f = fopen(fn, "wb"); fwrite(prof_n, 8, 0x80000, f); fclose(f);
        screenshot("final");
    }
    if (evf) fclose(evf);
    if (framef) fclose(framef);
    {   int b; fprintf(stderr, "cambio de bufer (lineas tras el vblank): max %.1f en el cuadro %ld; histograma:", swap_max, swap_max_fr);
        for (b = 0; b < 64; b++) if (swap_hist[b]) fprintf(stderr, " %d:%ld", b, swap_hist[b]);
        fprintf(stderr, "\npila: sp minimo 0x%06x en el cuadro %ld\n", minsp, minsp_fr);
        if (minsp_spad < 0x400) fprintf(stderr, "pila en el scratchpad: sp minimo 0x1F800%03x en el cuadro %ld (PC %08x): %u bytes libres\n", minsp_spad, minsp_spad_fr, minsp_spad_pc, minsp_spad); }
    if (misslog) fclose(misslog);
    fprintf(stderr, "perfil desde ciclo %llu (%.2f s)\n", (unsigned long long)prof_from_cyc, (cyc - prof_from_cyc) / CLK);
    fprintf(stderr, "SPU: %ld teclas pisadas antes de que su voz las viera, %ld saltos por repeticion\n", spu_perdidas, spu_saltos);
    if (o_glitch || o_glitch5) fprintf(stderr, "CD_GLITCH: %ld INT3 y %ld INT5 (leidos sondeando al cambiar: %ld y %ld), %ld lecturas de HINTSTS a medias\n", cd_gl_n3, cd_gl_n5, cd_gl_e3, cd_gl_e5, cd_gl_hechos);
    if (o_cola) fprintf(stderr, "CD_COLA: %ld lecturas de 1F801801-3 en el banco anterior\n", cola_n);
    if (o_t1g) fprintf(stderr, "T1_GLITCH: %ld lecturas del temporizador 1 al cambiar con acarreo, %ld rotas\n", t1_lect, t1_rotas);
    if (o_irq) fprintf(stderr, "LOADER_IRQ: %ld interrupciones, main %s\n", irq_n, irq_main ? "alcanzado" : "NO alcanzado");
    wav_hasta(cyc); wav_cierra();
    fprintf(stderr, "fin: %ld cuadros de juego, %llu vblanks, %.2f s emulados, %.1f cuadros de juego/s\n", game_frame, (unsigned long long)vbl_count,
            cyc / CLK, game_frame / (cyc / CLK));
    return 0;
}
