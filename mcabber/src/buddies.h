#ifndef __BUDDIES_H__
#define __BUDDIES_H__ 1

#include <ncurses.h>
#include "jabglue.h"
#include "list.h"

/* Definición de tipos */
typedef struct _buddy_entry_t {
  char *jid;
  char *name;
  char *group;
  char *resource;
  int flags;
  struct list_head list;
} buddy_entry_t;

void bud_DrawRoster(WINDOW * win);
void bud_RosterDown(void);
void bud_RosterUp(void);
void bud_TerminateBuddies(void);
int  bud_BuddyCount(void);
void bud_SetBuddyStatus(char *jidfrom, enum imstatus status);
void bud_SortRoster(void);
buddy_entry_t *bud_SelectedInfo(void);

buddy_entry_t *bud_AddBuddy(const char *bjid, const char *bname);
void           bud_DeleteBuddy(buddy_entry_t *buddy);

#endif
