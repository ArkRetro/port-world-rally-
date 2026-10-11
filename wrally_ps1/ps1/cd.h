#ifndef CD_H
#define CD_H
int cd_init(void);
int cd_read(int lba, int n, void *buf);
int cd_find(const char *name, int *lba, int *size);
int cd_track_lba(int t);
void cd_volume(int v);
int cd_play_at(int lba);
void cd_stop_audio(void);
#endif
