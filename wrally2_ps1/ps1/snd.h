/* Sonido del World Rally 2 en la PS1: los trozos del GAE1 en la SPU (diseno 5, formato de tools/sndwr2.py) */
#ifndef SND_H
#define SND_H
void snd_init(void);                            /* SPU parada y limpia; poner gae1_suceso = snd_suceso */
void snd_suceso(int ch, int t);                 /* gancho del modelo del GAE1 (rc/gae1.c) */
void snd_vblank(void);                          /* una vez por cuadro: encolados pendientes, cifras */
void snd_pausa(int on);                         /* 1: voces a tono 0 (pausa y cargas con el juego quieto) */
int snd_cargar(const char *nombre, unsigned addr);   /* fichero del CD a la RAM de la SPU, bloqueante; 0 si falta */
void snd_zona(int fichero);                     /* que fichero de la zona variable esta cargado (1-10; 0 ninguno) */
extern unsigned snd_kon, snd_encolados, snd_sin_trozo, snd_tarde;   /* cifras */
#endif
