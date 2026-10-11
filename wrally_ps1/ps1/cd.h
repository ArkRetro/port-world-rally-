#ifndef CD_H
#define CD_H
int cd_init(void);
void cd_poll(void);
int cd_busy(void);
void cd_sync(void);
void cd_speed(int fast);
int cd_read(int lba, int n, void *buf, int fast);
int cd_stream(int lba, int n, void (*cb)(int i, const unsigned char *d), int fast);
int cd_read_async(int lba, int n, void *buf);
int cd_stream_async(int lba, int n, unsigned char *(*dst)(int i));
int cd_read_pos(void);
int cd_read_state(void);
void cd_cancel(void);
int cd_find(const char *name, int *lba, int *size);
void cd_volume(int v);
void cd_play(int lba);
void cd_stop(void);
int cd_audio(void);
int cd_playing(void);
void cd_loc_start(void);
int cd_loc_get(void);
int cd_where(void);
extern int cd_ended, cd_dbg[8];
#endif
