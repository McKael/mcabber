#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>
#include "screen.h"
#include "buddies.h"

#include "lang.h"
#include "utils.h"
#include "server.h"
#include "list.h"
#include "harddefines.h"

/* global vars for BUDDIES.C */
int buddySelected = 1;		/* Hold the selected Buddy  */
int buddyOffset = 0;		/* Hold the roster offset   */

static LIST_HEAD(buddy_list);
static LIST_HEAD(sorted_buddies);

#define buddy_entry(n) list_entry(n, buddy_entry_t, list)


void bud_SetBuddyStatus(char *jidfrom, int status)
{
  struct list_head *pos, *n;
  buddy_entry_t *tmp;
  int changed = 0;
  char *buffer = (char *) malloc(4096);

  list_for_each_safe(pos, n, &buddy_list) {
    tmp = buddy_entry(pos);
    if (!strcmp(tmp->jid, jidfrom)) {
      if (tmp->flags != status) {
	tmp->flags = status;
	changed = 1;
      }
      break;
    }
  }
  if (changed) {
    bud_DrawRoster(scr_GetRosterWindow());
    switch (status) {
    case FLAG_BUDDY_DISCONNECTED:
      sprintf(buffer, "--> %s %s!", jidfrom, i18n("disconected"));
      break;

    case FLAG_BUDDY_CONNECTED:
      sprintf(buffer, "--> %s %s!", jidfrom, i18n("connected"));
      break;
    }
    scr_LogPrint("%s", buffer);
  }
  free(buffer);
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
}

void bud_ParseBuddies(char *roster)
{
  buddy_entry_t *tmp = NULL;
  char *aux;
  char *p, *str;

  ut_WriteLog("[roster]: %s\n\n", roster);

  while ((aux = ut_strrstr(roster, "<item")) != NULL) {
    char *jid = getattr(aux, "jid='");
    char *name = getattr(aux, "name='");
    char *group = gettag(aux, "group='");

    *aux = '\0';

    tmp = (buddy_entry_t *) calloc(1, sizeof(buddy_entry_t));

    tmp->flags = FLAG_BUDDY_DISCONNECTED;

    if (strncmp(jid, "UNK", 3)) {
      char *res = strstr(jid, "/");
      if (res)
	*res = '\0';

      tmp->jid = (char *) malloc(strlen(jid) + 1);
      strcpy(tmp->jid, jid);
      free(jid);
    }

    if (strncmp(name, "UNK", 3)) {
      tmp->name = (char *) calloc(1, strlen(name) + 1);
      strcpy(tmp->name, name);
      free(name);
    } else {
      tmp->name = (char *) calloc(1, strlen(tmp->jid) + 1);
      str = strdup(tmp->jid);
      p = strstr(str, "@");
      if (p) {
	*p = '\0';
      }
      strncpy(tmp->name, str, 18);
      free(str);
    }

    if (strncmp(group, "UNK", 3)) {
      tmp->group = (char *) malloc(strlen(group) + 1);
      strcpy(tmp->group, group);
      free(group);
    }

    if (!strncmp(tmp->jid, "msn.", 4)) {
      sprintf(tmp->name, "%c MSN %c", 254, 254);
    }

    if (!STR_EMPTY(tmp->jid)) {
      list_add_tail(&tmp->list, &buddy_list);
    } else {
      if (tmp->jid)
	free(tmp->jid);
      if (tmp->name)
	free(tmp->name);
      if (tmp->group)
	free(tmp->group);
      free(tmp);
    }
  }
  free(roster);

  bud_SortRoster();
}

/* Desc: Initialize buddy list
 * 
 * In : none
 * Out: none
 *
 * Note: none
 */
void bud_InitBuddies(int sock)
{
  char *roster;
  roster = srv_getroster(sock);
  bud_ParseBuddies(roster);
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
  window_entry_t *wintmp;

  getmaxyx(win, maxy, maxx);


  /* cleanup of roster window */
  wattrset(win, COLOR_PAIR(COLOR_GENERAL));
  for (i = 1; i < maxy - 1; i++) {
    mvwprintw(win, i, 1, "");
    for (n = 2; n < maxx; n++)
      waddch(win, ' ');
  }

  i = 1;
  list_for_each_safe(pos, nn, &buddy_list) {

    char status = '?';
    char pending = ' ';

    if (fakeOffset > 0) {
      fakeOffset--;
      continue;
    }

    tmp = buddy_entry(pos);
    // FIXME: we should create a function instead of exporting this! :-(
    // Cf. revision ~28
    wintmp = scr_SearchWindow(tmp->jid);
    /*
    if (wintmp)
      scr_LogPrint("wintmp != NULL");
    else
      scr_LogPrint("wintmp == NULL");
    */
    if ((wintmp) && (wintmp->hidden_msg)) {
      pending = '#';
    }

    if ((tmp->flags && FLAG_BUDDY_CONNECTED) == 1) {
      status = 'o';
      if (i == (buddySelected - buddyOffset))
	wattrset(win, COLOR_PAIR(COLOR_BD_CONSEL));
      else
	wattrset(win, COLOR_PAIR(COLOR_BD_CON));
    } else {
      if (i == (buddySelected - buddyOffset))
	wattrset(win, COLOR_PAIR(COLOR_BD_DESSEL));
      else
	wattrset(win, COLOR_PAIR(COLOR_BD_DES));
    }
    mvwprintw(win, i, 1, "");
    for (n = 2; n < maxx; n++)
      waddch(win, ' ');
    //mvwprintw(win, i, (maxx - strlen(tmp->name)) / 2, "%s", tmp->name);
    mvwprintw(win, i, 1, " %c[%c] %.12s", pending, status, tmp->name);
    i++;
    if (i >= maxy - 1)
      break;
  }
  update_panels();
  doupdate();
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
  y -= 2;

  if (buddySelected < bud_BuddyCount()) {
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
  if (buddySelected > 1) {
    buddySelected--;
    if (buddySelected - buddyOffset < 1)
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
    if (i == buddySelected - 1) {
      return tmp;
    }
    i++;
  }
  return NULL;
}

void bud_AddBuddy(int sock)
{
  char *buffer = (char *) calloc(1, 1024);
  char *buffer2 = (char *) calloc(1, 1024);
  char *p, *str;
  buddy_entry_t *tmp;

  ut_CenterMessage(i18n("write jid here"), 60, buffer2);
  scr_CreatePopup(i18n("Add jid"), buffer2, 60, 1, buffer);

  if (!STR_EMPTY(buffer)) {
    tmp = (buddy_entry_t *) calloc(1, sizeof(buddy_entry_t));
    tmp->jid = (char *) malloc(strlen(buffer) + 1);
    strcpy(tmp->jid, buffer);
    tmp->name = (char *) malloc(strlen(buffer) + 1);

    str = strdup(buffer);
    p = strstr(str, "@");
    if (p) {
      *p = '\0';
    }
    strcpy(tmp->name, str);
    free(str);

    list_add_tail(&tmp->list, &buddy_list);
    buddySelected = 1;
    bud_DrawRoster(scr_GetRosterWindow());
    srv_AddBuddy(sock, tmp->jid);
  }
  free(buffer);
}

void bud_DeleteBuddy(int sock)
{
  buddy_entry_t *tmp = bud_SelectedInfo();
  srv_DelBuddy(sock, tmp->jid);
  list_del(&tmp->list);
  buddySelected = 1;
  bud_DrawRoster(scr_GetRosterWindow());
}
