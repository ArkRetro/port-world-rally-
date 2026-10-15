/* video.c: lista de la GPU (compartida con main.c) y la imagen de verdad de los menus en formato A (doc/formatos.md:
   tiles de 8 bpp en 10 paginas de 256 huecos, CLUT = las 64 paletas del juego en S0) y de la demo y su mapa en
   formato B (4 bpp en 17 bloques de 256 huecos, una CLUT de 16 por hueco). */
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
void vid_carga_ini(int sec, int titulo); void vid_carga_fin(void);   /* mientras dura la carga de la seccion sec, lo que sube (y lo suyo) no se desaloja; titulo: no a las paginas 6 y 7 */
void vid_sec_prot(u32 lo, u32 hi);  /* mascara de secciones del modo actual: se desalojan las ultimas */
int vid_hueco(u32 codigo);          /* hueco del codigo o -1 */
void vid_cargando(int fb);          /* pantalla de carga al estilo del juego en el bufer que se ve */
extern int vid_cifras;                       /* cifras de depuracion a la vista (main.c: L2 + R2) */
void vid_pausa(int fb);                       /* el bufer que se ve oscurecido con PAUSA */
void vid_cargando_barra(int fb, int hecho, int total);   /* su barra START-FINISH */
/* demo y mapa de la demo (formato B) */
int vid_lay(void);                  /* reparto: 0 A, 1 A con b12-b15 en B (titulo), 2 B */
void vid_lay_a(void);               /* a los menus: todo vacio */
void vid_lay_titulo(void);          /* titulo: paginas A 6 y 7 fuera, huecos B 3 072-4 095 para la prelectura */
void vid_lay_b(int guarda);         /* a B (negro antes del mapa); guarda: se quedan los huecos de la prelectura */
void vid_quita_mapa(void);          /* negro antes de la demo: fuera los tiles del mapa */
void vid_area(u8 *rec, const u8 *l2);   /* en el area de tramo: 10 B por hueco B (recetas) y los sectores de la L2 */
int vid_sube_b(const u8 *reg, u32 extra, int rot);   /* registro B (144 B) a un hueco (extra: 0x80 mapa, 0x10 VO fijo; rot: puede desalojar); -1 sin sitio, -2 ya estaba */
void vid_pon_l2(const u8 *reg, int j);   /* registro j de la L2 (en la RAM): su codigo apunta a el */
void vid_b(void);                   /* plano 0 + sprites + efectos con los huecos B */
extern u32 vid_b_l2, vid_b_clut, vid_b_otrapal, vid_b_muertos, vid_b_sinsitio, vid_b_efemal, vid_b_rot, vid_b_fijos, vid_b_maxl2;
extern u32 vid_faltas, vid_faltas_cuadro, vid_desalojos, vid_lineas, vid_vict_it, vid_sin_sitio, vid_sin_hoja;
#endif
