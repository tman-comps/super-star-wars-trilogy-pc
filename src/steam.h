#ifndef STEAM_H
#define STEAM_H
int steam_init(const char* base_dir);   // 1 if Steam achievements are available
void steam_unlock(int id);
void steam_tick(void);                  // once per frame
void steam_shutdown(void);
int steam_active(void);
#endif
