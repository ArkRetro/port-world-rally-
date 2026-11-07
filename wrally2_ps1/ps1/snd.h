/* Sonido del World Rally 2 en la PS1: los trozos del GAE1 en la SPU (diseno 5, formato de tools/sndwr2.py) */
#ifndef SND_H
#define SND_H
void snd_init(void);                            /* SPU parada y limpia, SPUFIJO y SPUTIT de SPU.BIN (despues de cd_init); poner gae1_suceso = snd_suceso */
void snd_suceso(int ch, int t);                 /* gancho del modelo del GAE1 (rc/gae1.c) */
void snd_vblank(void);                          /* una vez por cuadro: encolados pendientes, cifras */
void snd_pausa(int on);                         /* 1: voces a tono 0 (pausa y cargas con el juego quieto) */
void snd_atiende(void);                         /* sube el fichero pedido (snd_pide), bloqueando: el CD libre y tono 0 */
void snd_precarga(int f);                       /* sube el fichero f ya (en un negro), si no esta */
extern unsigned snd_lin_pausa;                     /* lineas con las voces en pausa (las pone a 0 quien las lee) */
extern int snd_juego;                          /* 1: modelo por cuadros del juego y tono escalado por fps (-DSND_JUEGO) */
extern int snd_pide;                            /* fichero de la SPU que hace falta (lo pide un trozo que no esta) */
extern int snd_modo_tit;                        /* 1 en las pantallas del titulo: los trozos repetidos, de SPUTIT */
extern unsigned snd_kon, snd_encolados, snd_sin_trozo, snd_tarde, snd_cargas, snd_carga_lin;   /* cifras */
extern unsigned snd_ck_mal, snd_ck_perdidos, snd_spu_mal;   /* sectores de SPU.BIN mal leidos, perdidos; escrituras a la SPU mal */
#endif
