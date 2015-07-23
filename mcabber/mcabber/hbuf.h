#ifndef __MCABBER_HBUF_H__
#define __MCABBER_HBUF_H__ 1

#include <time.h>
#include <glib.h>

// With current implementation a message must fit in a hbuf block,
// so we shouldn't choose a too small size.
#define HBB_BLOCKSIZE   8192    // > 20 please

// Flags:
// - ALLOC: the ptr data has been allocated, it can be freed
// - PERSISTENT: this is a new history line
#define HBB_FLAG_ALLOC      1
#define HBB_FLAG_PERSISTENT 2

#define HBB_PREFIX_IN         (1U<<0)
#define HBB_PREFIX_OUT        (1U<<1)
#define HBB_PREFIX_STATUS     (1U<<2)
#define HBB_PREFIX_AUTH       (1U<<3)
#define HBB_PREFIX_INFO       (1U<<4)
#define HBB_PREFIX_ERR        (1U<<5)
#define HBB_PREFIX_NOFLAG     (1U<<6)
#define HBB_PREFIX_HLIGHT_OUT (1U<<7)
#define HBB_PREFIX_HLIGHT     (1U<<8)
#define HBB_PREFIX_NONE       (1U<<9)
#define HBB_PREFIX_SPECIAL    (1U<<10)
#define HBB_PREFIX_PGPCRYPT   (1U<<11)
#define HBB_PREFIX_OTRCRYPT   (1U<<12)
#define HBB_PREFIX_CONT       (1U<<13)
#define HBB_PREFIX_RECEIPT    (1U<<14)
#define HBB_PREFIX_READMARK   (1U<<15)
#define HBB_PREFIX_DELAYED    (1U<<16)
#define HBB_PREFIX_CARBON     (1U<<17)

typedef struct {
  time_t timestamp;
  guint flags;
  unsigned mucnicklen;
  char *text;
} hbb_line;

void hbuf_add_line(GList **p_hbuf, const char *text, time_t timestamp,
        guint prefix_flags, guint width, guint maxhbufblocks,
        unsigned mucnicklen, gpointer xep184);
void hbuf_free(GList **p_hbuf);
void hbuf_rebuild(GList **p_hbuf, unsigned int width);
GList *hbuf_previous_persistent(GList *l_line);

hbb_line **hbuf_get_lines(GList *hbuf, unsigned int n);
GList *hbuf_search(GList *hbuf, int direction, const char *string);
GList *hbuf_jump_date(GList *hbuf, time_t t);
GList *hbuf_jump_percent(GList *hbuf, int pc);
GList *hbuf_jump_readmark(GList *hbuf);
gboolean hbuf_remove_receipt(GList *hbuf, gconstpointer xep184);
void hbuf_set_readmark(GList *hbuf, gboolean action);
void hbuf_remove_trailing_readmark(GList *hbuf);

void hbuf_dump_to_file(GList *hbuf, const char *filename);

guint hbuf_get_blocks_number(GList *p_hbuf);

#endif /* __MCABBER_HBUF_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
