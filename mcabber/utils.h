#ifndef __UTILS_H__
#define __UTILS_H__ 1

#include <ncurses.h>

char **ut_SplitMessage(char *mensaje, int *nsubmsgs, unsigned int maxlong);
void ut_InitDebug(int level);
void ut_WriteLog(const char *fmt, ...);
char *ut_strrstr(const char *s1, const char *s2);
char *getattr(char *buffer, char *what);
char *gettag(char *buffer, char *what);
void ut_CenterMessage(char *text, int width, char *output);



#endif
