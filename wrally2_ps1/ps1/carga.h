/* carga.c: el CD en los menus (cd.c del WR1 por ruta, todo por sondeo): emergencias de TILESA.BIN por codigo, secciones
   A de MENUS.PAK por pantalla (bloqueante en la transicion, el resto del modo de fondo). */
#ifndef CARGA_H
#define CARGA_H
#include "../rc/rc_rt.h"
void carga_init(void);                   /* cd_init y cd_find de los ficheros (al arrancar) */
void carga_poll(void);                   /* solo sondea el CD (en cualquier sitio) */
void carga_atiende(void);                /* sondea y sube a la VRAM lo que llego (solo con la GPU parada) */
void carga_falta(u32 codigo);            /* pide un codigo que no esta en la VRAM (emergencia) */
int carga_pantalla(u32 clave, int fb);   /* seccion de MENUS.PAK de la pantalla, bloqueante (CARGANDO si > 0,5 s); -1 si no hay */
void carga_baja(int sec, int sub);       /* video.c: desalojado un tile del sector sub de la seccion sec */
extern u32 carga_sectores, carga_emerg, carga_emerg_lin, carga_emerg_max, carga_bloq, carga_bloq_lin, carga_bloq_max, carga_fondo, carga_err, carga_hueco_max;
extern int carga_menus;                  /* secciones del indice de MENUS.PAK (0: no esta en el disco) */
#endif
