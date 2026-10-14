/* video.c: lista de la GPU (compartida con main.c) y la imagen de verdad de los menus en formato A (doc/formatos.md:
   tiles de 8 bpp en 10 paginas de 256 huecos, CLUT = las 64 paletas del juego en S0). */
#ifndef VIDEO_H
#define VIDEO_H
#include "../rc/rc_rt.h"
#define W 368
#define H 240
/* lista de la GPU: nodos de hasta 16 palabras (main.c) */
extern u32 *lp;
void gpu_wait(void); void dma_wait(void);
void l_begin(void); void l_room(int n); int l_full(int n); void l_send(void);
void rect16(int x, int y, u32 rgb); void rect(int x, int y, int w, int h, u32 rgb);
void digits(int x, int y, u32 v, int n); u32 rgb24(u32 i);
/* menus (formato A) */
void vid_init(void);                /* paletas, CLUT de efecto, tablas; la GPU parada */
void vid_marco(int fb);             /* cabecera de la lista: area de dibujo, fondo y una CLUT falsa (cache de la GPU) */
void vid_paletas(void);             /* sube a S0 las paletas que cambiaron (con la GPU parada) */
void vid_menu(void);                /* plano 0 + sprites (copia del vblank anterior) + efectos con los huecos A */
void vid_spr_copia(void);           /* copia S(N) de los sprites activos de los dos bancos (al final de cada vblank) */
int vid_sube_a(const u8 *reg, int sec, int sub);  /* registro A (162 B) a un hueco (sec: seccion de MENUS y sector dentro de ella; 0xFF suelto); -1 sin sitio */
void vid_carga_ini(int sec); void vid_carga_fin(void);   /* mientras dura la carga de la seccion sec, lo que sube (y lo suyo) no se desaloja */
void vid_sec_prot(u32 lo, u32 hi);  /* mascara de secciones del modo actual: se desalojan las ultimas */
int vid_hueco(u32 codigo);          /* hueco del codigo o -1 */
void vid_cargando(int fb);          /* CARGANDO en el bufer que se ve */
extern u32 vid_faltas, vid_faltas_cuadro, vid_desalojos, vid_lineas, vid_vict_it, vid_sin_sitio, vid_sin_hoja;
#endif
