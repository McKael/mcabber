#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>
#include "screen.h"
#include "buddies.h"

#include "lang.h"
#include "utils.h"
#include "list.h"

#define STR_EMPTY(s) ((s)[0] == '\0')

/* global vars for BUDDIES.C */
int buddySelected = 0;		/* Hold the selected Buddy  */
int buddyOffset = 0;		/* Hold the roster offset   */

static LIST_HEAD(buddy_list);
static LIST_HEAD(sorted_buddies);

#define buddy_entry(n) list_entry(n, buddy_entry_t, list)


void bud_SetBuddyStatus(char *jidfrom, enum imstatus status)
{
  struct list_head *pos, *n;
  buddy_entry_t *tmp;
  enum imstatus oldstatus;
  int changed = 0;

  list_for_each_safe(pos, n, &buddy_list) {
    tmp = buddy_entry(pos);
    if (!strcasecmp(tmp->jid, jidfrom)) {
      if ((unsigned)tmp->flags != status) {
        oldstatus = tmp->flags;
	tmp->flags = status;
	changed = 1;
      }
      break;
    }
  }
  if (changed) {
    bud_DrawRoster(scr_GetRosterWindow());
    scr_LogPrint("Buddy status has changed: [%c>%c] <%s>",
            imstatus2char[oldstatus], imstatus2char[status], jidfrom);
  }
}

int compara(buddy_entry_t * t1, buddy_entry_t * t2)
{
  const char *s1 =
      (const char *) (STR_EMPTY(t1->name) ? t1->jid : t1->name);
  const char *s2 =
      (const char *) (STR_EMPTY(t2->name) ? t2->jid : t2->name);
  return strcasecmp(s1, s2);
}

void bud_SortRoster(void)
{
  buddy_entry_t *indice, *tmp;
  struct list_head *pos, *n;

  while (!list_empty(&buddy_list)) {
    indice = NULL;
    tmp = NULL;
    list_for_each_safe(pos, n, &buddy_list) {
      if (!indice) {
	indice = buddy_entry(pos);
	tmp = buddy_entry(pos);
      } else {
	tmp = buddy_entry(pos);
	if (compara(indice, tmp) > 0) {
	  indice = tmp;
	}
      }
    }
    list_move_tail(&indice->list, &sorted_buddies);
  }
  list_splice(&sorted_buddies, &buddy_list);

  update_roster = TRUE;
}

/* Desc: Destroy (and free) buddy list
 * 
 * In : none
 * Out: none
 *
 * Note: none
 */
void bud_TerminateBuddies(void)
{
}

/* Desc: Count elements in buddy list
 * 
 * In : none
 * Out: number of buddies
 *
 * Note: none
 */
int bud_BuddyCount(void)
{
  int i = 0;
  struct list_head *pos, *n;

  list_for_each_safe(pos, n, &buddy_list) {
    i++;
  }
  return i;
}

/* Desc: Draw the roster in roster window
 * 
 * In : roster window
 * Out: none
 *
 * Note: none
 */
void bud_DrawRoster(WINDOW * win)
{
  buddy_entry_t *tmp = NULL;
  struct list_head *pos, *nn;
  int i = 1;
  int n;
  int maxx, maxy;
  int fakeOffset = buddyOffset;
  char name[ROSTER_WIDTH];

  getmaxyx(win, maxy, maxx);
  maxx --;  // last char is for vertical border
  name[ROSTER_WIDTH-8] = 0;

  /* cleanup of roster window */
  wattrset(win, COLOR_PAIR(COLOR_GENERAL));
  for (i = 0; i < maxy; i++) {
    mvwprintw(win, i, 0, "");
    for (n = 0; n < maxx; n++)
      waddch(win, ' ');
  }

  i = 0;
  list_for_each_safe(pos, nn, &buddy_list) {

    char status = '?';
    char pending = ' ';

    if (fakeOffset > 0) {
      fakeOffset--;
      continue;
    }

    tmp = buddy_entry(pos);
    if (scr_IsHiddenMessage(tmp->jid)) {
      pending = '#';
    }

    if (tmp->flags >= 0 && tmp->flags < imstatus_size) {
      status = imstatus2char[tmp->flags];
    }
    /*{
      if (i == (buddySelected - buddyOffset))
	wattrset(win, COLOR_PAIR(COLOR_BD_CONSEL));
      else
	wattrset(win, COLOR_PAIR(COLOR_BD_CON));
    } else*/ {
      if (i == (buddySelected - buddyOffset))
	wattrset(win, COLOR_PAIR(COLOR_BD_DESSEL));
      else
	wattrset(win, COLOR_PAIR(COLOR_BD_DES));
    }
    mvwprintw(win, i, 0, "");
    for (n = 2; n < maxx; n++)
      waddch(win, ' ');
    strncpy(name, tmp->name, ROSTER_WIDTH-8);
    mvwprintw(win, i, 0, " %c[%c] %s", pending, status, name);
    i++;
    if (i >= maxy - 1)
      break;
  }
  update_panels();
  doupdate();

  update_roster = FALSE;
}

/* Desc: Change selected buddy (one position down)
 * 
 * In : none
 * Out: none
 *
 * Note: none
 */
void bud_RosterDown(void)
{
  int x, y;
  getmaxyx(scr_GetRosterWindow(), y, x);

  if (buddySelected+1 < bud_BuddyCount()) {
    buddySelected++;
    if (buddySelected > y)
      buddyOffset++;
    bud_DrawRoster(scr_GetRosterWindow());
  }
}

/* Desc: Change selected buddy (one position up)
 * 
 * In : none
 * Out: none
 *
 * Note: none
 */
void bud_RosterUp(void)
{
  if (buddySelected > 0) {
    buddySelected--;
    if (buddySelected < buddyOffset)
      buddyOffset--;
    bud_DrawRoster(scr_GetRosterWindow());
  }
}

/* Desc: Retrieve info for selected buddy
 * 
 * In : none
 * Out: (buddy_entry_t *) of selected buddy
 *
 * Note: none
 */
buddy_entry_t *bud_SelectedInfo(void)
{
  struct list_head *pos, *n;
  buddy_entry_t *tmp = NULL;

  int i = 0;

  list_for_each_safe(pos, n, &buddy_list) {
    tmp = buddy_entry(pos);
    if (i == buddySelected) {
      return tmp;
    }
    i++;
  }
  return NULL;
}

buddy_entry_t *bud_AddBuddy(const char *bjid, const char *bname)
{
  char *p, *str;
  buddy_entry_t *tmp;

  tmp = calloc(1, sizeof(buddy_entry_t));
  tmp->jid = strdup(bjid);

  if (bname) {
    tmp->name = strdup(bname);
  } else {
    str = strdup(bjid);
    p = strstr(str, "/");
    if (p)  *p = '\0';
    tmp->name = strdup(str);
    free(str);
  }

  ut_WriteLog("Adding buddy: %s <%s>\n", tmp->name, tmp->jid);

  list_add_tail(&tmp->list, &buddy_list);
  bud_DrawRoster(scr_GetRosterWindow());

  return tmp;
}

void bud_DeleteBuddy(buddy_entry_t *buddy)
{
  list_del(&buddy->list);
  buddySelected = 1;
  bud_DrawRoster(scr_GetRosterWindow());
}
