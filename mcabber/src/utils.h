#ifndef __UTILS_H__
#define __UTILS_H__ 1

void ut_InitDebug(unsigned int level, const char *file);
void ut_WriteLog(const char *fmt, ...);

int    to_iso8601(char *dststr, time_t timestamp);
time_t from_iso8601(const char *timestamp, int utc);

#endif
