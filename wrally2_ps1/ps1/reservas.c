/* Buferes del port que aun no existen, con su tamano final (diseno 2.1): el EXE R0 de H2 ocupa ya la RAM que ocupara
   el port entero, asi que la RAM libre de las cifras y la puerta G-RAM (tools/gram.py) son las de verdad. Cada bufer
   sale de aqui cuando se escribe su codigo. Con -DR1 (EXE de tamanos R1, solo se enlaza para medir el area de tramo):
   ademas el codigo que falta hasta los presupuestos (RES_CODIGO, de gram.py) */
typedef unsigned char u8;
/* H3: tslot, datos por hueco, copia de sprites, paletas/CLUT, CD y cargador ya existen (video.c, carga.c, cd.c);
   el sonido tambien (snd.c y la tabla de trozos) */
static struct {
    u8 proxies[4096];       /* pen medio por grupo de 8 codigos */
} res __attribute__((aligned(4)));
#ifdef R1
/* rc_run hasta 417 792 B y el C del port hasta 81 920 B: en la imagen (datos iniciados), donde ira ese codigo, y no en
   la BSS; asi el relleno de la imagen a 2 KB es el del EXE con todo el codigo y no depende de cuanto haya hoy (antes el
   area cambiaba hasta 2 KB con unos bytes de codigo de mas o de menos) */
static u8 codigo[RES_CODIGO] __attribute__((aligned(4), section(".data.res_codigo")));
void reservas(void) { *(volatile u8 *)res.proxies = 0; *(volatile u8 *)codigo = 0; }
#else
void reservas(void) { *(volatile u8 *)res.proxies = 0; }   /* referencia: --gc-sections no la quita */
#endif
