/* Buferes del port que aun no existen, con su tamano final (diseno 2.1): el EXE R0 de H2 ocupa ya la RAM que ocupara
   el port entero, asi que la RAM libre de las cifras y la puerta G-RAM (tools/gram.py) son las de verdad. Cada bufer
   sale de aqui cuando se escribe su codigo. Con -DR1 (EXE de tamanos R1, solo se enlaza para medir el area de tramo):
   ademas la ROM residente, rc_romtab y el codigo que falta hasta los presupuestos (RES_CODIGO, de gram.py) */
typedef unsigned char u8;
/* H3: tslot, datos por hueco, copia de sprites, paletas/CLUT, CD y cargador ya existen (video.c, carga.c, cd.c) */
static struct {
    u8 proxies[4096];       /* pen medio por grupo de 8 codigos */
    u8 sonido[6144];        /* calendario de voces y snd_tab (el modelo del GAE1 va en la VRAM del 68000) */
#ifdef R1
    u8 romtab[16384];       /* rc_romtab[4096] */
    u8 codigo[RES_CODIGO];  /* rc_run hasta 417 792 B y el C del port hasta 81 920 B */
#endif
} res __attribute__((aligned(4)));
void reservas(void) { *(volatile u8 *)res.proxies = 0; }   /* referencia: --gc-sections no la quita */
