/* carga.c: el CD (cd.c del WR1 por ruta, todo por sondeo): emergencias de TILESA.BIN (A) o TILES.BIN (B) por codigo,
   secciones A de MENUS.PAK por pantalla (bloqueante en la transicion, el resto del modo de fondo) y el paquete de la
   demo siguiente (DEMOr.PAK: mapa y VO a la VRAM, L2 y ROM al area de tramo). */
#ifndef CARGA_H
#define CARGA_H
#include "../rc/rc_rt.h"
void carga_init(void);                   /* cd_init y cd_find de los ficheros (al arrancar) */
void carga_poll(void);                   /* solo sondea el CD (en cualquier sitio) */
void carga_atiende(void);                /* sondea y sube a la VRAM lo que llego (solo con la GPU parada) */
void carga_falta(u32 codigo);            /* pide un codigo que no esta en la VRAM (emergencia) */
int carga_pantalla(u32 clave, int fb, int parcial);   /* seccion de MENUS.PAK de la pantalla, bloqueante (CARGANDO si > 0,5 s);
                                                       parcial: bit 0 solo el principio, lo demas de fondo;
                                                       bit 1 si no esta, la mayor del modo; -1 si no hay */
void carga_baja(int sec, int sub);       /* video.c: desalojado un tile del sector sub de la seccion sec */
void carga_demo_titulo(int r);           /* titulo (3/16): prelectura de la demo r de fondo */
void carga_demo_negro(int ped, int fb);  /* negro antes del mapa (ped 9 / 0xB) o de la demo (ped 5) o la carrera (1): lo que falte, bloqueando */
void carga_demo_modo(int modo, int fb);        /* cada cuadro del mapa (9, 0xB), la demo (5) o la carrera (1): comprueba el paquete */
void carga_demo_fin(void);
void carga_para(void);
#ifdef R1
int carga_rom_arranque(void);            /* ROM.BIN entera al area y la trampa puesta; -1 si no se puede */
void carga_rom_suma(void);               /* fin de la suma del arranque: solo lo residente, el area libre */
extern u32 carga_rom_trampas, carga_rom_lin, carga_rom_max, carga_rom_kb;
#endif                   /* corta el fondo y la emergencia: el CD libre para una lectura de otro */               /* vuelta a los menus (reparto A) */
extern u32 carga_emerg_sec, carga_dm_neg[2], carga_dm_neg_max[2], carga_dm_neg_n[2], carga_dm_fallo, carga_dm_nositio, carga_area_kb, carga_l2_cap;
extern u32 carga_sectores, carga_emerg, carga_emerg_lin, carga_emerg_max, carga_bloq, carga_bloq_lin, carga_bloq_max, carga_fondo, carga_err, carga_hueco_max;
extern int carga_menus;                  /* secciones del indice de MENUS.PAK (0: no esta en el disco) */
#endif
void carga_relleno(int fb, u32 hechas);
/* pila (main.c): el juego corre con la pila en el scratchpad (1 KB, un ciclo por acceso); lo que bloquea y llama
   hondo (cargas, la trampa de la ROM, la pausa) va con en_ram en la pila de la RAM */
void en_ram(void (*f)(u32), u32 a);
#define PILA_RAM 8192u                   /* lo que se deja para la pila de la RAM (lo mas hondo medido: ~3 KB al arrancar);
                                            antes 16 KB, con todo el juego en ella (tools/gram.py, PILA) */   /* en un negro: CARGANDO hasta 3 s en total, cargando de fondo lo que venga */
