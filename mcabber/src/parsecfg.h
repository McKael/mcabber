#ifndef __PARSECFG_H__
#define __PARSECFG_H__ 1

int cfg_file(char *filename);
char *cfg_read(char *key);
int cfg_read_int(char *key);

#endif
