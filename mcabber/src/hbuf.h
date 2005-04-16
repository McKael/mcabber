#ifndef __HBUF_H__
#define __HBUF_H__ 1

#include <glib.h>

#define HBB_BLOCKSIZE   1024    // > 20 please

// Flags:
// - ALLOC: the ptr data has been allocated, it can be freed
// - PERSISTENT: this is a new history line
#define HBB_FLAG_ALLOC      1
#define HBB_FLAG_PERSISTENT 2
// #define HBB_FLAG_FREE       4

void hbuf_add_line(GList **p_hbuf, char *text, unsigned int width);
void hbuf_free(GList **p_hbuf);
void hbuf_rebuild(GList **p_hbuf, unsigned int width);

char **hbuf_get_lines(GList *hbuf, unsigned int n);

#endif /* __HBUF_H__ */
