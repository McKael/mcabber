#ifndef __UTILS_H__
#define __UTILS_H__ 1

void ut_InitDebug(unsigned int level, const char *file);
void ut_WriteLog(unsigned int flag, const char *data);

int checkset_perm(const char *name, unsigned int setmode);

int    to_iso8601(char *dststr, time_t timestamp);
time_t from_iso8601(const char *timestamp, int utc);

inline void safe_usleep(unsigned int usec); /* Only for delays < 1s */

#endif
