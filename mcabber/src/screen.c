/*
 * screen.c     -- UI stuff
 *
 * Copyright (C) 2005, 2006 Mikael Berthe <bmikael@lists.lilotux.net>
 * Parts of this file come from the Cabber project <cabber@ajmacias.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>
#include <time.h>
#include <ctype.h>
#include <locale.h>
#include <langinfo.h>

#include "screen.h"
#include "hbuf.h"
#include "commands.h"
#include "compl.h"
#include "roster.h"
#include "histolog.h"
#include "settings.h"
#include "utils.h"
#include "list.h"

#define window_entry(n) list_entry(n, window_entry_t, list)

#define DEFAULT_LOG_WIN_HEIGHT (5+2)
#define DEFAULT_ROSTER_WIDTH    24
#define CHAT_WIN_HEIGHT (maxY-1-Log_Win_Height)

char *LocaleCharSet = "C";

static unsigned short int Log_Win_Height;
static unsigned short int Roster_Width;

static inline void check_offset(int);

LIST_HEAD(window_list);

typedef struct _window_entry_t {
  WINDOW *win;
  PANEL  *panel;
  char   *name;
  GList  *hbuf;
  GList  *top;      // If top is NULL, we'll display the last lines
  char    cleared;  // For ex, user has issued a /clear command...
  struct list_head list;
} window_entry_t;


static WINDOW *rosterWnd, *chatWnd, *inputWnd, *logWnd;
static WINDOW *mainstatusWnd, *chatstatusWnd;
static PANEL *rosterPanel, *chatPanel, *inputPanel;
static PANEL *mainstatusPanel, *chatstatusPanel;
static PANEL *logPanel;
static int maxY, maxX;
static window_entry_t *currentWindow;

static int roster_hidden;
static int chatmode;
static int multimode;
static char *multiline;
int update_roster;
int utf8_mode = 0;
static bool Autoaway;
static bool Curses;

static char       inputLine[INPUTLINE_LENGTH+1];
static char      *ptr_inputline;
static short int  inputline_offset;
static int    completion_started;
static GList *cmdhisto;
static GList *cmdhisto_cur;
static char   cmdhisto_backup[INPUTLINE_LENGTH+1];


/* Functions */

static int scr_WindowWidth(WINDOW * win)
{
  int x, y;
  getmaxyx(win, y, x);
  return x;
}

static int FindColor(const char *name)
{
  if (!strcmp(name, "default"))
    return -1;
  if (!strcmp(name, "black"))
    return COLOR_BLACK;
  if (!strcmp(name, "red"))
    return COLOR_RED;
  if (!strcmp(name, "green"))
    return COLOR_GREEN;
  if (!strcmp(name, "yellow"))
    return COLOR_YELLOW;
  if (!strcmp(name, "blue"))
    return COLOR_BLUE;
  if (!strcmp(name, "magenta"))
    return COLOR_MAGENTA;
  if (!strcmp(name, "cyan"))
    return COLOR_CYAN;
  if (!strcmp(name, "white"))
    return COLOR_WHITE;

  return -1;
}

static void ParseColors(void)
{
  const char *colors[8] = {
    "", "",
    "general",
    "status",
    "roster",
    "rostersel",
    "rosternewmsg",
    NULL
  };

  char *tmp = g_new(char, 512);
  const char *color;
  const char *background   = settings_opt_get("color_background");
  const char *backselected = settings_opt_get("color_bgrostersel");
  const char *backstatus   = settings_opt_get("color_bgstatus");
  int i = 0;

  // Default values
  if (!background)   background   = "black";
  if (!backselected) backselected = "cyan";
  if (!backstatus)   backstatus   = "blue";

  while (colors[i]) {
    snprintf(tmp, 512, "color_%s", colors[i]);
    color = settings_opt_get(tmp);

    switch (i + 1) {
    case 1:
      init_pair(1, COLOR_BLACK, COLOR_WHITE);
      break;
    case 2:
      init_pair(2, COLOR_WHITE, COLOR_BLACK);
      break;
    case COLOR_GENERAL:
      init_pair(i+1, ((color) ? FindColor(color) : COLOR_WHITE),
                FindColor(background));
      break;
    case COLOR_STATUS:
      init_pair(i+1, ((color) ? FindColor(color) : COLOR_WHITE),
                FindColor(backstatus));
      break;
    case COLOR_ROSTER:
      init_pair(i+1, ((color) ? FindColor(color) : COLOR_GREEN),
                FindColor(background));
      break;
    case COLOR_ROSTERSEL:
      init_pair(i+1, ((color) ? FindColor(color) : COLOR_BLUE),
                FindColor(backselected));
      break;
    case COLOR_ROSTERNMSG:
      init_pair(i+1, ((color) ? FindColor(color) : COLOR_RED),
                FindColor(background));
      break;
    }
    i++;
  }
}

void scr_InitCurses(void)
{
  initscr();
  raw();
  noecho();
  nonl();
  intrflush(stdscr, FALSE);
  start_color();
  use_default_colors();
  Curses = TRUE;

  ParseColors();

  getmaxyx(stdscr, maxY, maxX);
  Log_Win_Height = DEFAULT_LOG_WIN_HEIGHT;
  // Note scr_DrawMainWindow() should be called early after scr_InitCurses()
  // to update Log_Win_Height and set max{X,Y}

  inputLine[0] = 0;
  ptr_inputline = inputLine;

  setlocale(LC_CTYPE, "");
  LocaleCharSet = nl_langinfo(CODESET);
  utf8_mode = (strcmp(LocaleCharSet, "UTF-8") == 0);

  return;
}

void scr_TerminateCurses(void)
{
  if (!Curses) return;
  clear();
  refresh();
  endwin();
  Curses = FALSE;
  return;
}

inline void scr_Beep(void)
{
  beep();
}

//  scr_LogPrint(...)
// Display a message in the log window.
void scr_LogPrint(unsigned int flag, const char *fmt, ...)
{
  time_t timestamp;
  char *buffer, *b2;
  va_list ap;

  if (!flag) return;

  buffer = g_new(char, 5184);

  timestamp = time(NULL);
  strftime(buffer, 48, "[%H:%M:%S] ", localtime(&timestamp));
  for (b2 = buffer ; *b2 ; b2++)
    ;
  va_start(ap, fmt);
  vsnprintf(b2, 5120, fmt, ap);
  va_end(ap);

  if (flag & LPRINT_NORMAL) {
    if (Curses) {
      wprintw(logWnd, "\n%s", buffer);
      update_panels();
      doupdate();
    } else {
      printf("%s\n", buffer);
    }
  }
  if (flag & (LPRINT_LOG|LPRINT_DEBUG)) {
    char *buffer2 = g_new(char, 5184);

    strftime(buffer2, 23, "[%Y-%m-%d %H:%M:%S] ", localtime(&timestamp));
    strcat(buffer2, b2);
    strcat(buffer2, "\n");
    ut_WriteLog(flag, buffer2);
    g_free(buffer2);
  }
  g_free(buffer);
}

static window_entry_t *scr_CreateBuddyPanel(const char *title, int dont_show)
{
  int x;
  int y;
  int lines;
  int cols;
  window_entry_t *tmp;

  tmp = g_new0(window_entry_t, 1);

  // Dimensions
  x = Roster_Width;
  y = 0;
  lines = CHAT_WIN_HEIGHT;
  cols = maxX - Roster_Width;
  if (cols < 1) cols = 1;

  tmp->win = newwin(lines, cols, y, x);
  while (!tmp->win) {
    safe_usleep(250);
    tmp->win = newwin(lines, cols, y, x);
  }
  wbkgd(tmp->win, COLOR_PAIR(COLOR_GENERAL));
  tmp->panel = new_panel(tmp->win);
  tmp->name = g_strdup(title);

  if (!dont_show) {
    currentWindow = tmp;
  } else {
    if (currentWindow)
      top_panel(currentWindow->panel);
    else
      top_panel(chatPanel);
  }
  update_panels();

  // Load buddy history from file (if enabled)
  hlog_read_history(title, &tmp->hbuf, maxX - Roster_Width - PREFIX_WIDTH);

  list_add_tail(&tmp->list, &window_list);

  return tmp;
}

static window_entry_t *scr_SearchWindow(const char *winId)
{
  struct list_head *pos, *n;
  window_entry_t *search_entry = NULL;

  if (!winId) return NULL;

  list_for_each_safe(pos, n, &window_list) {
    search_entry = window_entry(pos);
    if (search_entry->name) {
      if (!strcasecmp(search_entry->name, winId)) {
	return search_entry;
      }
    }
  }
  return NULL;
}

bool scr_BuddyBufferExists(const char *jid)
{
  return (scr_SearchWindow(jid) != NULL);
}

//  scr_UpdateWindow()
// (Re-)Display the given chat window.
static void scr_UpdateWindow(window_entry_t *win_entry)
{
  int n;
  int width;
  hbb_line **lines, *line;
  GList *hbuf_head;
  char date[32];

  width = scr_WindowWidth(win_entry->win);

  // Should the window be empty?
  if (win_entry->cleared) {
    werase(win_entry->win);
    return;
  }

  // win_entry->top is the top message of the screen.  If it set to NULL, we
  // are displaying the last messages.

  // We will show the last CHAT_WIN_HEIGHT lines.
  // Let's find out where it begins.
  if (!win_entry->top ||
      (g_list_position(g_list_first(win_entry->hbuf), win_entry->top) == -1)) {
    // Move up CHAT_WIN_HEIGHT lines
    win_entry->hbuf = g_list_last(win_entry->hbuf);
    hbuf_head = win_entry->hbuf;
    win_entry->top = NULL; // (Just to make sure)
    n = 0;
    while (hbuf_head && (n < CHAT_WIN_HEIGHT-1) && g_list_previous(hbuf_head)) {
      hbuf_head = g_list_previous(hbuf_head);
      n++;
    }
  } else
    hbuf_head = win_entry->top;

  // Get the last CHAT_WIN_HEIGHT lines.
  lines = hbuf_get_lines(hbuf_head, CHAT_WIN_HEIGHT);

  // Display these lines
  for (n = 0; n < CHAT_WIN_HEIGHT; n++) {
    wmove(win_entry->win, n, 0);
    line = *(lines+n);
    // NOTE: update PREFIX_WIDTH if you change the date format!!
    // You need to set it to the whole prefix length + 1
    if (line) {
      if (line->timestamp) {
        strftime(date, 30, "%m-%d %H:%M", localtime(&line->timestamp));
      } else
        strcpy(date, "           ");
      if (line->flags & HBB_PREFIX_INFO) {
        char dir = '*';
        if (line->flags & HBB_PREFIX_IN)
          dir = '<';
        else if (line->flags & HBB_PREFIX_OUT)
          dir = '>';
        wprintw(win_entry->win, "%.11s *%c* ", date, dir);
      } else if (line->flags & HBB_PREFIX_ERR) {
        char dir = '#';
        if (line->flags & HBB_PREFIX_IN)
          dir = '<';
        else if (line->flags & HBB_PREFIX_OUT)
          dir = '>';
        wprintw(win_entry->win, "%.11s #%c# ", date, dir);
      } else if (line->flags & HBB_PREFIX_IN)
        wprintw(win_entry->win, "%.11s <== ", date);
      else if (line->flags & HBB_PREFIX_OUT)
        wprintw(win_entry->win, "%.11s --> ", date);
      else {
        wprintw(win_entry->win, "%.11s     ", date);
      }
      wprintw(win_entry->win, "%s", line->text);      // line
      wclrtoeol(win_entry->win);
      g_free(line->text);
    } else {
      wclrtobot(win_entry->win);
      break;
    }
  }
  g_free(lines);
}

//  scr_ShowWindow()
// Display the chat window with the given identifier.
static void scr_ShowWindow(const char *winId)
{
  window_entry_t *win_entry = scr_SearchWindow(winId);

  if (!win_entry)
    win_entry = scr_CreateBuddyPanel(winId, FALSE);

  top_panel(win_entry->panel);
  currentWindow = win_entry;
  chatmode = TRUE;
  roster_msg_setflag(winId, FALSE);
  roster_setflags(winId, ROSTER_FLAG_LOCK, TRUE);
  update_roster = TRUE;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();

  top_panel(inputPanel);
}

//  scr_ShowBuddyWindow()
// Display the chat window buffer for the current buddy.
void scr_ShowBuddyWindow(void)
{
  const gchar *jid;

  if (!current_buddy)
    jid = NULL;
  else
    jid = CURRENT_JID;

  if (!jid) {
    top_panel(chatPanel);
    top_panel(inputPanel);
    currentWindow = NULL;
    return;
  }

  scr_ShowWindow(jid);
}

//  scr_WriteInWindow()
// Write some text in the winId window (this usually is a jid).
// Lines are splitted when they are too long to fit in the chat window.
// If this window doesn't exist, it is created.
void scr_WriteInWindow(const char *winId, const char *text, time_t timestamp,
        unsigned int prefix_flags, int force_show)
{
  window_entry_t *win_entry;
  int dont_show = FALSE;

  // Look for the window entry.
  win_entry = scr_SearchWindow(winId);

  // Do we have to really show the window?
  if (!chatmode)
    dont_show = TRUE;
  else if ((!force_show) && ((!currentWindow || (currentWindow != win_entry))))
    dont_show = TRUE;

  // If the window entry doesn't exist yet, let's create it.
  if (win_entry == NULL) {
    win_entry = scr_CreateBuddyPanel(winId, dont_show);
  }

  // The message must be displayed -> update top pointer
  if (win_entry->cleared)
    win_entry->top = g_list_last(win_entry->hbuf);

  hbuf_add_line(&win_entry->hbuf, text, timestamp, prefix_flags,
                maxX - Roster_Width - PREFIX_WIDTH);

  if (win_entry->cleared) {
    win_entry->cleared = FALSE;
    if (g_list_next(win_entry->top))
      win_entry->top = g_list_next(win_entry->top);
  }

  // Make sure the last line appears in the window; update top if necessary
  if (win_entry->top) {
    int dist;
    GList *first = g_list_first(win_entry->hbuf);
    dist = g_list_position(first, g_list_last(win_entry->hbuf)) -
           g_list_position(first, win_entry->top);
    if (dist >= CHAT_WIN_HEIGHT)
      win_entry->top = NULL;
  }

  if (!dont_show) {
    // Show and refresh the window
    top_panel(win_entry->panel);
    scr_UpdateWindow(win_entry);
    top_panel(inputPanel);
    update_panels();
    doupdate();
  } else if (!(prefix_flags & HBB_PREFIX_NOFLAG)) {
    roster_msg_setflag(winId, TRUE);
    update_roster = TRUE;
  }
}

//  scr_UpdateMainStatus()
// Redraw the main (bottom) status line.
void scr_UpdateMainStatus(void)
{
  const char *sm = jb_getstatusmsg();

  werase(mainstatusWnd);
  mvwprintw(mainstatusWnd, 0, 0, "%c[%c] %s", 
            (unread_msg(NULL) ? '#' : ' '),
            imstatus2char[jb_getstatus()], (sm ? sm : ""));
  top_panel(inputPanel);
  update_panels();
  doupdate();
}

//  scr_DrawMainWindow()
// Set fullinit to TRUE to also create panels.  Set it to FALSE for a resize.
//
// I think it could be improved a _lot_ but I'm really not an ncurses
// expert... :-\   Mikael.
//
void scr_DrawMainWindow(unsigned int fullinit)
{
  int requested_size;

  Log_Win_Height = DEFAULT_LOG_WIN_HEIGHT;
  requested_size = settings_opt_get_int("log_win_height");
  if (requested_size > 0) {
    if (maxY > requested_size + 3)
      Log_Win_Height = requested_size + 2;
    else
      Log_Win_Height = ((maxY > 5) ? (maxY - 2) : 3);
  } else if (requested_size < 0) {
    Log_Win_Height = 3;
  }

  if (maxY < Log_Win_Height+2) {
    if (maxY < 5) {
      Log_Win_Height = 3;
      maxY = Log_Win_Height+2;
    } else {
      Log_Win_Height = maxY - 2;
    }
  }

  if (roster_hidden) {
    Roster_Width = 0;
  } else {
    requested_size = settings_opt_get_int("roster_width");
    if (requested_size > 1)
      Roster_Width = requested_size;
    else if (requested_size == 1)
      Roster_Width = 2;
    else
      Roster_Width = DEFAULT_ROSTER_WIDTH;
  }

  if (fullinit) {
    /* Create windows */
    rosterWnd = newwin(CHAT_WIN_HEIGHT, Roster_Width, 0, 0);
    chatWnd   = newwin(CHAT_WIN_HEIGHT, maxX - Roster_Width, 0, Roster_Width);
    logWnd    = newwin(Log_Win_Height-2, maxX, CHAT_WIN_HEIGHT+1, 0);
    chatstatusWnd = newwin(1, maxX, CHAT_WIN_HEIGHT, 0);
    mainstatusWnd = newwin(1, maxX, maxY-2, 0);
    inputWnd  = newwin(1, maxX, maxY-1, 0);
    if (!rosterWnd || !chatWnd || !logWnd || !inputWnd) {
      scr_TerminateCurses();
      fprintf(stderr, "Cannot create windows!\n");
      exit(EXIT_FAILURE);
    }
    wbkgd(rosterWnd,      COLOR_PAIR(COLOR_GENERAL));
    wbkgd(chatWnd,        COLOR_PAIR(COLOR_GENERAL));
    wbkgd(logWnd,         COLOR_PAIR(COLOR_GENERAL));
    wbkgd(chatstatusWnd,  COLOR_PAIR(COLOR_STATUS));
    wbkgd(mainstatusWnd,  COLOR_PAIR(COLOR_STATUS));
  } else {
    /* Resize/move windows */
    wresize(rosterWnd, CHAT_WIN_HEIGHT, Roster_Width);
    wresize(chatWnd, CHAT_WIN_HEIGHT, maxX - Roster_Width);
    mvwin(chatWnd, 0, Roster_Width);

    wresize(logWnd, Log_Win_Height-2, maxX);
    mvwin(logWnd, CHAT_WIN_HEIGHT+1, 0);

    // Resize & move chat status window
    wresize(chatstatusWnd, 1, maxX);
    mvwin(chatstatusWnd, CHAT_WIN_HEIGHT, 0);
    // Resize & move main status window
    wresize(mainstatusWnd, 1, maxX);
    mvwin(mainstatusWnd, maxY-2, 0);
    // Resize & move input line window
    wresize(inputWnd, 1, maxX);
    mvwin(inputWnd, maxY-1, 0);

    werase(chatWnd);
  }

  /* Draw/init windows */

  mvwprintw(chatWnd, 0, 0, "Thanks for using mcabber.\n");
  mvwprintw(chatWnd, 1, 0, "http://www.lilotux.net/~mikael/mcabber/");

  // Auto-scrolling in log window
  scrollok(logWnd, TRUE);


  if (fullinit) {
    // Enable keypad (+ special keys)
    keypad(inputWnd, TRUE);
    nodelay(inputWnd, TRUE);

    // Create panels
    rosterPanel = new_panel(rosterWnd);
    chatPanel   = new_panel(chatWnd);
    logPanel    = new_panel(logWnd);
    chatstatusPanel = new_panel(chatstatusWnd);
    mainstatusPanel = new_panel(mainstatusWnd);
    inputPanel  = new_panel(inputWnd);

    if (utf8_mode)
      scr_LogPrint(LPRINT_NORMAL, "WARNING: UTF-8 not yet supported!");
  } else {
    // Update panels
    replace_panel(rosterPanel, rosterWnd);
    replace_panel(chatPanel, chatWnd);
    replace_panel(logPanel, logWnd);
    replace_panel(chatstatusPanel, chatstatusWnd);
    replace_panel(mainstatusPanel, mainstatusWnd);
    replace_panel(inputPanel, inputWnd);
  }

  // We'll need to redraw the roster
  update_roster = TRUE;
  return;
}

//  scr_Resize()
// Function called when the window is resized.
// - Resize windows
// - Rewrap lines in each buddy buffer
void scr_Resize()
{
  struct list_head *pos, *n;
  window_entry_t *search_entry;
  int x, y, lines, cols;

  // First, update the global variables
  getmaxyx(stdscr, maxY, maxX);
  // scr_DrawMainWindow() will take care of maxY and Log_Win_Height

  // Make sure the cursor stays inside the window
  check_offset(0);

  // Resize windows and update panels
  scr_DrawMainWindow(FALSE);

  // Resize all buddy windows
  x = Roster_Width;
  y = 0;
  lines = CHAT_WIN_HEIGHT;
  cols = maxX - Roster_Width;
  if (cols < 1) cols = 1;

  list_for_each_safe(pos, n, &window_list) {
    search_entry = window_entry(pos);
    if (search_entry->win) {
      GList *rescue_top;
      // Resize/move buddy window
      wresize(search_entry->win, lines, cols);
      mvwin(search_entry->win, 0, Roster_Width);
      werase(search_entry->win);
      // If a panel exists, replace the old window with the new
      if (search_entry->panel) {
        replace_panel(search_entry->panel, search_entry->win);
      }
      // Redo line wrapping
      rescue_top = hbuf_previous_persistent(search_entry->top);
      hbuf_rebuild(&search_entry->hbuf,
              maxX - Roster_Width - PREFIX_WIDTH);
      if (g_list_position(g_list_first(search_entry->hbuf),
                          search_entry->top) == -1) {
        search_entry->top = rescue_top;
      }
    }
  }

  // Refresh current buddy window
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  update_chat_status_window(forceupdate)
// Redraw the buddy status bar.
// Set forceupdate to TRUE if doupdate() must be called.
static void update_chat_status_window(int forceupdate)
{
  unsigned short btype, isgrp, ismuc;
  const char *fullname;
  const char *msg = NULL;
  char status;
  char *buf;

  // Usually we need to update the bottom status line too,
  // at least to refresh the pending message flag.
  scr_UpdateMainStatus();

  fullname = buddy_getname(BUDDATA(current_buddy));
  btype = buddy_gettype(BUDDATA(current_buddy));

  isgrp = btype & ROSTER_TYPE_GROUP;
  ismuc = btype & ROSTER_TYPE_ROOM;

  // Clear the line
  werase(chatstatusWnd);

  if (chatmode)
    wprintw(chatstatusWnd, "©");

  if (isgrp) {
    mvwprintw(chatstatusWnd, 0, 5, "Group: %s", fullname);
    if (forceupdate) {
      update_panels();
      doupdate();
    }
    return;
  }

  status = '?';

  if (ismuc) {
    if (buddy_getinsideroom(BUDDATA(current_buddy)))
      status = 'C';
    else
      status = 'x';
  } else if (jb_getstatus() != offline) {
    enum imstatus budstate;
    budstate = buddy_getstatus(BUDDATA(current_buddy), NULL);
    if (budstate >= 0 && budstate < imstatus_size)
      status = imstatus2char[budstate];
  }

  // No status message for groups & MUC rooms
  if (!isgrp && !ismuc) {
    GSList *resources = buddy_getresources(BUDDATA(current_buddy));
    if (resources)
      msg = buddy_getstatusmsg(BUDDATA(current_buddy), resources->data);
  }
  if (!msg)
    msg = "";

  buf = g_strdup_printf("[%c] Buddy: %s -- %s", status, fullname, msg);
  replace_nl_with_dots(buf);
  mvwprintw(chatstatusWnd, 0, 1, "%s", buf);
  g_free(buf);

  if (forceupdate) {
    update_panels();
    doupdate();
  }
}

//  scr_DrawRoster()
// Display the buddylist (not really the roster) on the screen
void scr_DrawRoster(void)
{
  static guint offset = 0;
  char *name, *rline;
  int maxx, maxy;
  GList *buddy;
  int i, n;
  int rOffset;
  int cursor_backup;
  char status, pending;
  enum imstatus currentstatus = jb_getstatus();

  // We can reset update_roster
  update_roster = FALSE;

  getmaxyx(rosterWnd, maxy, maxx);
  maxx--;  // Last char is for vertical border

  cursor_backup = curs_set(0);

  // Cleanup of roster window
  werase(rosterWnd);

  if (Roster_Width) {
    // Redraw the vertical line (not very good...)
    wattrset(rosterWnd, COLOR_PAIR(COLOR_GENERAL));
    for (i=0 ; i < CHAT_WIN_HEIGHT ; i++)
      mvwaddch(rosterWnd, i, Roster_Width-1, ACS_VLINE);
  }

  if (!buddylist)
    offset = 0;
  else
    update_chat_status_window(FALSE);

  // Leave now if buddylist is empty or the roster is hidden
  if (!buddylist || !Roster_Width) {
    update_panels();
    doupdate();
    curs_set(cursor_backup);
    return;
  }

  name = g_new0(char, Roster_Width);

  // Update offset if necessary
  // a) Try to show as many buddylist items as possible
  i = g_list_length(buddylist) - maxy;
  if (i < 0)
    i = 0;
  if (i < offset)
    offset = i;
  // b) Make sure the current_buddy is visible
  i = g_list_position(buddylist, current_buddy);
  if (i == -1) { // This is bad
    scr_LogPrint(LPRINT_NORMAL, "Doh! Can't find current selected buddy!!");
    g_free(name);
    curs_set(cursor_backup);
    return;
  } else if (i < offset) {
    offset = i;
  } else if (i+1 > offset + maxy) {
    offset = i + 1 - maxy;
  }

  rline = g_new0(char, Roster_Width+1);

  buddy = buddylist;
  rOffset = offset;

  for (i=0; i<maxy && buddy; buddy = g_list_next(buddy)) {
    unsigned short bflags, btype, ismsg, isgrp, ismuc, ishid;

    bflags = buddy_getflags(BUDDATA(buddy));
    btype = buddy_gettype(BUDDATA(buddy));

    ismsg = bflags & ROSTER_FLAG_MSG;
    ishid = bflags & ROSTER_FLAG_HIDE;
    isgrp = btype  & ROSTER_TYPE_GROUP;
    ismuc = btype  & ROSTER_TYPE_ROOM;

    if (rOffset > 0) {
      rOffset--;
      continue;
    }

    status = '?';
    pending = ' ';

    // Display message notice if there is a message flag, but not
    // for unfolded groups.
    if (ismsg && (!isgrp || ishid)) {
      pending = '#';
    }

    if (ismuc) {
      if (buddy_getinsideroom(BUDDATA(buddy)))
        status = 'C';
      else
        status = 'x';
    } else if (currentstatus != offline) {
      enum imstatus budstate;
      budstate = buddy_getstatus(BUDDATA(buddy), NULL);
      if (budstate >= 0 && budstate < imstatus_size)
        status = imstatus2char[budstate];
    }
    if (buddy == current_buddy) {
      wattrset(rosterWnd, COLOR_PAIR(COLOR_ROSTERSEL));
      // The 3 following lines aim to color the whole line
      wmove(rosterWnd, i, 0);
      for (n = 0; n < maxx; n++)
        waddch(rosterWnd, ' ');
    } else {
      if (pending == '#')
        wattrset(rosterWnd, COLOR_PAIR(COLOR_ROSTERNMSG));
      else
        wattrset(rosterWnd, COLOR_PAIR(COLOR_ROSTER));
    }

    if (Roster_Width > 7)
      strncpy(name, buddy_getname(BUDDATA(buddy)), Roster_Width-7);
    else
      name[0] = 0;

    if (isgrp) {
      char *sep;
      if (ishid)
        sep = "+++";
      else
        sep = "---";
      snprintf(rline, Roster_Width, " %c%s %s", pending, sep, name);
    } else {
      snprintf(rline, Roster_Width, " %c[%c] %s", pending, status, name);
    }

    mvwprintw(rosterWnd, i, 0, "%s", rline);
    i++;
  }

  g_free(rline);
  g_free(name);
  top_panel(inputPanel);
  update_panels();
  doupdate();
  curs_set(cursor_backup);
}

//  scr_RosterVisibility(status)
// Set the roster visibility:
// status=1   Show roster
// status=0   Hide roster
// status=-1  Toggle roster status
void scr_RosterVisibility(int status)
{
  int old_roster_status = roster_hidden;

  if (status > 0)
    roster_hidden = FALSE;
  else if (status == 0)
    roster_hidden = TRUE;
  else
    roster_hidden = !roster_hidden;

  if (roster_hidden != old_roster_status) {
    if (roster_hidden) {
      // Enter chat mode
      scr_set_chatmode(TRUE);
      scr_ShowBuddyWindow();
    }
    // Recalculate windows size and redraw
    scr_Resize();
    redrawwin(stdscr);
  }
}

inline void scr_WriteMessage(const char *jid, const char *text,
                             time_t timestamp, guint prefix_flags)
{
  if (!timestamp) timestamp = time(NULL);

  scr_WriteInWindow(jid, text, timestamp, prefix_flags, FALSE);
}

// If prefix is NULL, HBB_PREFIX_IN is supposed.
void scr_WriteIncomingMessage(const char *jidfrom, const char *text,
        time_t timestamp, guint prefix)
{
  if (!(prefix & ~HBB_PREFIX_NOFLAG))
    prefix |= HBB_PREFIX_IN;
  // FIXME expand tabs / filter out special chars...
  scr_WriteMessage(jidfrom, text, timestamp, prefix);
  update_panels();
  doupdate();
}

void scr_WriteOutgoingMessage(const char *jidto, const char *text)
{
  scr_WriteMessage(jidto, text, 0, HBB_PREFIX_OUT);
  scr_ShowWindow(jidto);
}

void inline set_autoaway(bool setaway)
{
  static enum imstatus oldstatus;
  static char *oldmsg;
  Autoaway = setaway;

  if (setaway) {
    const char *msg, *prevmsg;
    oldstatus = jb_getstatus();
    if (oldmsg) {
      g_free(oldmsg);
      oldmsg = NULL;
    }
    prevmsg = jb_getstatusmsg();
    msg = settings_opt_get("message_autoaway");
    if (!msg)
      msg = prevmsg;
    if (prevmsg)
      oldmsg = g_strdup(prevmsg);
    jb_setstatus(away, NULL, msg);
  } else {
    // Back
    jb_setstatus(oldstatus, NULL, (oldmsg ? oldmsg : ""));
    if (oldmsg) {
      g_free(oldmsg);
      oldmsg = NULL;
    }
  }
}

// Check if we should enter/leave automatic away status
void scr_CheckAutoAway(bool activity)
{
  static time_t LastActivity;
  enum imstatus cur_st;
  unsigned int autoaway_timeout = settings_opt_get_int("autoaway");

  if (Autoaway && activity) set_autoaway(FALSE);
  if (!autoaway_timeout) return;
  if (!LastActivity || activity) time(&LastActivity);

  cur_st = jb_getstatus();
  // Auto-away is disabled for the following states
  if ((cur_st != available) && (cur_st != freeforchat))
    return;

  if (!activity) {
    time_t now;
    time(&now);
    if (!Autoaway && (now > LastActivity + autoaway_timeout))
      set_autoaway(TRUE);
  }
}

//  set_current_buddy(newbuddy)
// Set the current_buddy to newbuddy (if not NULL)
// Lock the newbuddy, and unlock the previous current_buddy
static void set_current_buddy(GList *newbuddy)
{
  enum imstatus prev_st = imstatus_size;
  /* prev_st initialized to imstatus_size, which is used as "undef" value.
   * We are sure prev_st will get a different status value after the
   * buddy_getstatus() call.
   */

  if (!current_buddy || !newbuddy)  return;
  if (newbuddy == current_buddy)    return;

  prev_st = buddy_getstatus(BUDDATA(current_buddy), NULL);
  buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
  if (chatmode)
    alternate_buddy = current_buddy;
  current_buddy = newbuddy;
  // Lock the buddy in the buddylist if we're in chat mode
  if (chatmode)
    buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, TRUE);
  // We should rebuild the buddylist but not everytime
  // Here we check if we were locking a buddy who is actually offline,
  // and hide_offline_buddies is TRUE.  In which case we need to rebuild.
  if (prev_st == offline && buddylist_get_hide_offline_buddies())
    buddylist_build();
  update_roster = TRUE;
}

//  scr_RosterTop()
// Go to the first buddy in the buddylist
void scr_RosterTop(void)
{
  set_current_buddy(buddylist);
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterBottom()
// Go to the last buddy in the buddylist
void scr_RosterBottom(void)
{
  set_current_buddy(g_list_last(buddylist));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterUp()
// Go to the previous buddy in the buddylist
void scr_RosterUp(void)
{
  set_current_buddy(g_list_previous(current_buddy));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterDown()
// Go to the next buddy in the buddylist
void scr_RosterDown(void)
{
  set_current_buddy(g_list_next(current_buddy));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterSearch(str)
// Look forward for a buddy with jid/name containing str.
void scr_RosterSearch(char *str)
{
  set_current_buddy(buddy_search(str));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterJumpJid(jid)
// Jump to buddy jid.
// NOTE: With this function, the buddy is added to the roster if doesn't exist.
void scr_RosterJumpJid(char *barejid)
{
  GSList *roster_elt;
  // Look for an existing buddy
  roster_elt = roster_find(barejid, jidsearch,
                 ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM);
  // Create it if necessary
  if (!roster_elt)
    roster_elt = roster_add_user(barejid, NULL, NULL, ROSTER_TYPE_USER,
                                 sub_none);
  // Set a lock to see it in the buddylist
  buddy_setflags(BUDDATA(roster_elt), ROSTER_FLAG_LOCK, TRUE);
  buddylist_build();
  // Jump to the buddy
  set_current_buddy(buddy_search_jid(barejid));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterUnreadMessage(next)
// Go to a new message.  If next is not null, try to go to the next new
// message.  If it is not possible or if next is NULL, go to the first new
// message from unread_list.
void scr_RosterUnreadMessage(int next)
{
  gpointer unread_ptr;
  gpointer refbuddata;
  gpointer ngroup;
  GList *nbuddy;

  if (!current_buddy) return;

  if (next) refbuddata = BUDDATA(current_buddy);
  else      refbuddata = NULL;

  unread_ptr = unread_msg(refbuddata);
  if (!unread_ptr) return;

  // If buddy is in a folded group, we need to expand it
  ngroup = buddy_getgroup(unread_ptr);
  if (buddy_getflags(ngroup) & ROSTER_FLAG_HIDE) {
    buddy_setflags(ngroup, ROSTER_FLAG_HIDE, FALSE);
    buddylist_build();
  }

  nbuddy = g_list_find(buddylist, unread_ptr);
  if (nbuddy) {
    set_current_buddy(nbuddy);
    if (chatmode) scr_ShowBuddyWindow();
  } else
    scr_LogPrint(LPRINT_LOGNORM, "Error: nbuddy == NULL"); // should not happen
}

//  scr_RosterJumpAlternate()
// Try to jump to alternate (== previous) buddy
void scr_RosterJumpAlternate(void)
{
  if (!alternate_buddy || g_list_position(buddylist, alternate_buddy) == -1)
    return;
  set_current_buddy(alternate_buddy);
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_BufferScrollUpDown()
// Scroll up/down the current buddy window,
// - half a screen if nblines is 0,
// - up if updown == -1, down if updown == 1
void scr_BufferScrollUpDown(int updown, unsigned int nblines)
{
  window_entry_t *win_entry;
  int n, nbl;
  GList *hbuf_top;

  // Get win_entry
  if (!current_buddy) return;
  win_entry  = scr_SearchWindow(CURRENT_JID);
  if (!win_entry) return;

  if (!nblines) {
    // Scroll half a screen (or less)
    nbl = CHAT_WIN_HEIGHT/2;
  } else {
    nbl = nblines;
  }
  hbuf_top = win_entry->top;

  if (updown == -1) {   // UP
    if (!hbuf_top) {
      hbuf_top = g_list_last(win_entry->hbuf);
      if (!win_entry->cleared) {
        if (!nblines) nbl = nbl*3 - 1;
        else nbl += CHAT_WIN_HEIGHT - 1;
      } else {
        win_entry->cleared = FALSE;
      }
    }
    for (n=0 ; hbuf_top && n < nbl && g_list_previous(hbuf_top) ; n++)
      hbuf_top = g_list_previous(hbuf_top);
    win_entry->top = hbuf_top;
  } else {              // DOWN
    for (n=0 ; hbuf_top && n < nbl ; n++)
      hbuf_top = g_list_next(hbuf_top);
    win_entry->top = hbuf_top;
    // Check if we are at the bottom
    for (n=0 ; hbuf_top && n < CHAT_WIN_HEIGHT-1 ; n++)
      hbuf_top = g_list_next(hbuf_top);
    if (!hbuf_top)
      win_entry->top = NULL; // End reached
  }

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_BufferClear()
// Clear the current buddy window (used for the /clear command)
void scr_BufferClear(void)
{
  window_entry_t *win_entry;

  // Get win_entry
  if (!current_buddy) return;
  win_entry  = scr_SearchWindow(CURRENT_JID);
  if (!win_entry) return;

  win_entry->cleared = TRUE;
  win_entry->top = NULL;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_BufferTopBottom()
// Jump to the head/tail of the current buddy window
// (top if topbottom == -1, bottom topbottom == 1)
void scr_BufferTopBottom(int topbottom)
{
  window_entry_t *win_entry;

  // Get win_entry
  if (!current_buddy) return;
  win_entry  = scr_SearchWindow(CURRENT_JID);
  if (!win_entry) return;

  win_entry->cleared = FALSE;
  if (topbottom == 1)
    win_entry->top = NULL;
  else
    win_entry->top = g_list_first(win_entry->hbuf);

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_BufferSearch(direction, text)
// Jump to the next line containing text
// (backward search if direction == -1, forward if topbottom == 1)
void scr_BufferSearch(int direction, const char *text)
{
  window_entry_t *win_entry;
  GList *current_line, *search_res;

  // Get win_entry
  if (!current_buddy) return;
  win_entry  = scr_SearchWindow(CURRENT_JID);
  if (!win_entry) return;

  if (win_entry->top)
    current_line = win_entry->top;
  else
    current_line = g_list_last(win_entry->hbuf);

  search_res = hbuf_search(current_line, direction, text);

  if (search_res) {
    win_entry->cleared = FALSE;
    win_entry->top = search_res;

    // Refresh the window
    scr_UpdateWindow(win_entry);

    // Finished :)
    update_panels();
    doupdate();
  } else
    scr_LogPrint(LPRINT_NORMAL, "Search string not found");
}

//  scr_BufferPercent(n)
// Jump to the specified position in the buffer, in %
void scr_BufferPercent(int pc)
{
  window_entry_t *win_entry;
  GList *search_res;

  // Get win_entry
  if (!current_buddy) return;
  win_entry  = scr_SearchWindow(CURRENT_JID);
  if (!win_entry) return;

  if (pc < 0 || pc > 100) {
    scr_LogPrint(LPRINT_NORMAL, "Bad % value");
    return;
  }

  search_res = hbuf_jump_percent(win_entry->hbuf, pc);

  win_entry->cleared = FALSE;
  win_entry->top = search_res;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_BufferDate(t)
// Jump to the first line after date t in the buffer
// t is a date in seconds since `00:00:00 1970-01-01 UTC'
void scr_BufferDate(time_t t)
{
  window_entry_t *win_entry;
  GList *search_res;

  // Get win_entry
  if (!current_buddy) return;
  win_entry  = scr_SearchWindow(CURRENT_JID);
  if (!win_entry) return;

  search_res = hbuf_jump_date(win_entry->hbuf, t);

  win_entry->cleared = FALSE;
  win_entry->top = search_res;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_set_chatmode()
// Public function to (un)set chatmode...
inline void scr_set_chatmode(int enable)
{
  chatmode = enable;
  update_chat_status_window(TRUE);
}

//  scr_get_multimode()
// Public function to get multimode status...
inline int scr_get_multimode()
{
  return multimode;
}

//  scr_setmsgflag_if_needed(jid)
// Set the message flag unless we're already in the jid buffer window
void scr_setmsgflag_if_needed(const char *jid)
{
  const char *current_jid;

  if (current_buddy)
    current_jid = buddy_getjid(BUDDATA(current_buddy));
  else
    current_jid = NULL;
  if (!chatmode || !current_jid || strcmp(jid, current_jid))
    roster_msg_setflag(jid, TRUE);
}

//  scr_set_multimode()
// Public function to (un)set multimode...
// Convention:
//  0 = disabled / 1 = multimode / 2 = multimode verbatim (commands disabled)
inline void scr_set_multimode(int enable)
{
  if (multiline) {
    g_free(multiline);
    multiline = NULL;
  }
  multimode = enable;
}

//  scr_get_multiline()
// Public function to get the current multi-line.
inline const char *scr_get_multiline()
{
  if (multimode && multiline)
    return multiline;
  else
    return "";
}

//  scr_append_multiline(line)
// Public function to append a line to the current multi-line message.
// Skip empty leading lines.
void scr_append_multiline(const char *line)
{
  static int num;

  if (!multimode) {
    scr_LogPrint(LPRINT_NORMAL, "Error: Not in multi-line message mode!");
    return;
  }
  if (multiline) {
    int len = strlen(multiline)+strlen(line)+2;
    if (len >= HBB_BLOCKSIZE - 1) {
      // We don't handle single messages with size > HBB_BLOCKSIZE
      // (see hbuf)
      scr_LogPrint(LPRINT_NORMAL, "Your multi-line message is too big, "
                   "this line has not been added.");
      scr_LogPrint(LPRINT_NORMAL, "Please send this part now...");
      return;
    }
    if (num >= MULTILINE_MAX_LINE_NUMBER) {
      // We don't allow too many lines; however the maximum is arbitrary
      // (It should be < 1000 yet)
      scr_LogPrint(LPRINT_NORMAL, "Your message has too many lines, "
                   "this one has not been added.");
      scr_LogPrint(LPRINT_NORMAL, "Please send this part now...");
      return;
    }
    multiline = g_renew(char, multiline, len);
    strcat(multiline, "\n");
    strcat(multiline, line);
    num++;
  } else {
    // First message line (we skip leading empty lines)
    num = 0;
    if (line[0]) {
      multiline = g_strdup(line);
      num++;
    } else
      return;
  }
  scr_LogPrint(LPRINT_NORMAL, "Multi-line mode: line #%d added  [%.25s...",
               num, line);
}

//  scr_cmdhisto_addline()
// Add a line to the inputLine history
inline void scr_cmdhisto_addline(char *line)
{
  if (!line || !*line) return;

  cmdhisto = g_list_append(cmdhisto, g_strdup(line));
}

//  scr_cmdhisto_prev()
// Look for previous line beginning w/ the given mask in the inputLine history
// Returns NULL if none found
static const char *scr_cmdhisto_prev(char *mask, guint len)
{
  GList *hl;
  if (!cmdhisto_cur) {
    hl = g_list_last(cmdhisto);
    if (hl) { // backup current line
      strncpy(cmdhisto_backup, mask, INPUTLINE_LENGTH);
    }
  } else {
    hl = g_list_previous(cmdhisto_cur);
  }
  while (hl) {
    if (!strncmp((char*)hl->data, mask, len)) {
      // Found a match
      cmdhisto_cur = hl;
      return (const char*)hl->data;
    }
    hl = g_list_previous(hl);
  }
  return NULL;
}

//  scr_cmdhisto_next()
// Look for next line beginning w/ the given mask in the inputLine history
// Returns NULL if none found
static const char *scr_cmdhisto_next(char *mask, guint len)
{
  GList *hl;
  if (!cmdhisto_cur) return NULL;
  hl = cmdhisto_cur;
  while ((hl = g_list_next(hl)) != NULL)
    if (!strncmp((char*)hl->data, mask, len)) {
      // Found a match
      cmdhisto_cur = hl;
      return (const char*)hl->data;
    }
  // If the "backuped" line matches, we'll use it
  if (strncmp(cmdhisto_backup, mask, len)) return NULL; // No match
  cmdhisto_cur = NULL;
  return cmdhisto_backup;
}

//  readline_transpose_chars()
// Drag  the  character  before point forward over the character at
// point, moving point forward as well.  If point is at the end  of
// the  line, then this transposes the two characters before point.
void readline_transpose_chars()
{
  char swp;

  if (ptr_inputline == inputLine) return;

  if (!*ptr_inputline) { // We're at EOL
    // If line is only 1 char long, nothing to do...
    if (ptr_inputline == inputLine+1) return;
    // Transpose the two previous characters
    swp = *(ptr_inputline-2);
    *(ptr_inputline-2) = *(ptr_inputline-1);
    *(ptr_inputline-1) = swp;
  } else {
    // Swap the two characters before the cursor and move right.
    swp = *(ptr_inputline-1);
    *(ptr_inputline-1) = *ptr_inputline;
    *ptr_inputline++ = swp;
    check_offset(1);
  }
}

//  readline_backward_kill_word()
// Kill the word before the cursor, in input line
void readline_backward_kill_word()
{
  char *c, *old = ptr_inputline;
  int spaceallowed = 1;

  if (ptr_inputline == inputLine) return;

  for (c = ptr_inputline-1 ; c > inputLine ; c--)
    if (!isalnum(*c)) {
      if (*c == ' ')
        if (!spaceallowed) break;
    } else spaceallowed = 0;

  if (c != inputLine || *c != ' ')
    if ((c < ptr_inputline-1) && (!isalnum(*c)))
      c++;

  // Modify the line
  ptr_inputline = c;
  for (;;) {
    *c = *old++;
    if (!*c++) break;
  }
  check_offset(-1);
}

//  which_row()
// Tells which row our cursor is in, in the command line.
// -2 -> normal text
// -1 -> room: nickname completion
//  0 -> command
//  1 -> parameter 1 (etc.)
//  If > 0, then *p_row is set to the beginning of the row
static int which_row(const char **p_row)
{
  int row = -1;
  char *p;
  int quote = FALSE;

  // Not a command?
  if ((ptr_inputline == inputLine) || (inputLine[0] != '/')) {
    if (!current_buddy) return -2;
    if (buddy_gettype(BUDDATA(current_buddy)) == ROSTER_TYPE_ROOM) {
      *p_row = inputLine;
      return -1;
    }
    return -2;
  }

  // This is a command
  row = 0;
  for (p = inputLine ; p < ptr_inputline ; p++) {
    if (quote) {
      if (*p == '"' && *(p-1) != '\\')
        quote = FALSE;
      continue;
    }
    if (*p == '"' && *(p-1) != '\\') {
      quote = TRUE;
    } else if (*p == ' ') {
      if (*(p-1) != ' ')
        row++;
      *p_row = p+1;
    }
  }
  return row;
}

//  scr_insert_text()
// Insert the given text at the current cursor position.
// The cursor is moved.  We don't check if the cursor still is in the screen
// after, the caller should do that.
static void scr_insert_text(const char *text)
{
  char tmpLine[INPUTLINE_LENGTH+1];
  int len = strlen(text);
  // Check the line isn't too long
  if (strlen(inputLine) + len >= INPUTLINE_LENGTH) {
    scr_LogPrint(LPRINT_LOGNORM, "Cannot insert text, line too long.");
    return;
  }

  strcpy(tmpLine, ptr_inputline);
  strcpy(ptr_inputline, text);
  ptr_inputline += len;
  strcpy(ptr_inputline, tmpLine);
}

//  scr_handle_tab()
// Function called when tab is pressed.
// Initiate or continue a completion...
static void scr_handle_tab(void)
{
  int nrow;
  const char *row;
  const char *cchar;
  guint compl_categ;

  nrow = which_row(&row);

  // a) No completion if no leading slash ('cause not a command),
  //    unless this is a room (then, it is a nickname completion)
  // b) We can't have more than 2 parameters (we use 2 flags)
  if ((nrow == -2) || (nrow == 3 && !completion_started) || nrow > 3)
    return;

  if (nrow == 0) {          // Command completion
    row = &inputLine[1];
    compl_categ = COMPL_CMD;
  } else if (nrow == -1) {  // Nickname completion
    compl_categ = COMPL_RESOURCE;
  } else {                  // Other completion, depending on the command
    int alias = FALSE;
    cmd *com;
    char *xpline = expandalias(inputLine);
    com = cmd_get(xpline);
    if (xpline != inputLine) {
      // This is an alias, so we can't complete rows > 0
      alias = TRUE;
      g_free(xpline);
    }
    if ((!com && (!alias || !completion_started)) || !row) {
      scr_LogPrint(LPRINT_NORMAL, "I cannot complete that...");
      return;
    }
    if (!alias)
      compl_categ = com->completion_flags[nrow-1];
    else
      compl_categ = 0;
  }

  if (!completion_started) {
    GSList *list = compl_get_category_list(compl_categ);
    if (list) {
      char *prefix = g_strndup(row, ptr_inputline-row);
      // Init completion
      new_completion(prefix, list);
      g_free(prefix);
      // Now complete
      cchar = complete();
      if (cchar)
        scr_insert_text(cchar);
      completion_started = TRUE;
    }
  } else {      // Completion already initialized
    char *c;
    guint back = cancel_completion();
    // Remove $back chars
    ptr_inputline -= back;
    c = ptr_inputline;
    for ( ; *c ; c++)
      *c = *(c+back);
    // Now complete again
    cchar = complete();
    if (cchar)
      scr_insert_text(cchar);
  }
}

static void scr_cancel_current_completion(void)
{
  char *c;
  guint back = cancel_completion();
  // Remove $back chars
  ptr_inputline -= back;
  c = ptr_inputline;
  for ( ; *c ; c++)
    *c = *(c+back);
}

static void scr_end_current_completion(void)
{
  done_completion();
  completion_started = FALSE;
}

//  check_offset(int direction)
// Check inputline_offset value, and make sure the cursor is inside the
// screen.
static inline void check_offset(int direction)
{
  // Left side
  if (inputline_offset && direction <= 0) {
    while (ptr_inputline <= (char*)&inputLine + inputline_offset) {
      if (inputline_offset) {
        inputline_offset -= 5;
        if (inputline_offset < 0)
          inputline_offset = 0;
      }
    }
  }
  // Right side
  if (direction >= 0) {
    while (ptr_inputline >= inputline_offset + (char*)&inputLine + maxX)
      inputline_offset += 5;
  }
}

static inline void refresh_inputline(void)
{
  mvwprintw(inputWnd, 0,0, "%s", inputLine + inputline_offset);
  wclrtoeol(inputWnd);
  if (*ptr_inputline)
    wmove(inputWnd, 0, ptr_inputline - (char*)&inputLine - inputline_offset);
}

void scr_handle_CtrlC(void)
{
  if (!Curses) return;
  // Leave multi-line mode
  process_command("/msay abort");
  // Same as Ctrl-g, now
  scr_cancel_current_completion();
  scr_end_current_completion();
  check_offset(-1);
  refresh_inputline();
}

int scr_Getch(void)
{
  int ch;
  ch = wgetch(inputWnd);
  return ch;
}

//  process_key(key)
// Handle the pressed key, in the command line (bottom).
int process_key(int key)
{
  switch(key) {
    case 8:     // Ctrl-h
    case 127:   // Backspace too
    case KEY_BACKSPACE:
        if (ptr_inputline != (char*)&inputLine) {
          char *c = --ptr_inputline;
          for ( ; *c ; c++)
            *c = *(c+1);
          check_offset(-1);
        }
        break;
    case KEY_DC:// Del
        if (*ptr_inputline)
          strcpy(ptr_inputline, ptr_inputline+1);
        break;
    case KEY_LEFT:
        if (ptr_inputline != (char*)&inputLine) {
          ptr_inputline--;
          check_offset(-1);
        }
        break;
    case KEY_RIGHT:
        if (*ptr_inputline)
          ptr_inputline++;
          check_offset(1);
        break;
    case 7:     // Ctrl-g
        scr_cancel_current_completion();
        scr_end_current_completion();
        check_offset(-1);
        break;
    case 9:     // Tab
        scr_handle_tab();
        check_offset(0);
        break;
    case 13:    // Enter
    case 15:    // Ctrl-o ("accept-line-and-down-history")
        scr_CheckAutoAway(TRUE);
        if (process_line(inputLine))
          return 255;
        // Add line to history
        scr_cmdhisto_addline(inputLine);
        // Reset the line
        ptr_inputline = inputLine;
        *ptr_inputline = 0;
        inputline_offset = 0;

        if (key == 13)            // Enter
        {
          // Reset history line pointer
          cmdhisto_cur = NULL;
        } else {                  // down-history
          // Use next history line instead of a blank line
          const char *l = scr_cmdhisto_next("", 0);
          if (l) strcpy(inputLine, l);
          // Reset backup history line
          cmdhisto_backup[0] = 0;
        }
        break;
    case KEY_UP:
        {
          const char *l = scr_cmdhisto_prev(inputLine,
                  ptr_inputline-inputLine);
          if (l) strcpy(inputLine, l);
        }
        break;
    case KEY_DOWN:
        {
          const char *l = scr_cmdhisto_next(inputLine,
                  ptr_inputline-inputLine);
          if (l) strcpy(inputLine, l);
        }
        break;
    case KEY_PPAGE:
        scr_CheckAutoAway(TRUE);
        scr_RosterUp();
        break;
    case KEY_NPAGE:
        scr_CheckAutoAway(TRUE);
        scr_RosterDown();
        break;
    case KEY_HOME:
    case 1:
        ptr_inputline = inputLine;
        inputline_offset = 0;
        break;
    case 3:     // Ctrl-C
        scr_handle_CtrlC();
        break;
    case KEY_END:
    case 5:
        for (; *ptr_inputline; ptr_inputline++) ;
        check_offset(1);
        break;
    case 21:    // Ctrl-u
        strcpy(inputLine, ptr_inputline);
        ptr_inputline = inputLine;
        inputline_offset = 0;
        break;
    case KEY_EOL:
    case 11:    // Ctrl-k
        *ptr_inputline = 0;
        break;
    case 16:    // Ctrl-p
        scr_BufferScrollUpDown(-1, 0);
        break;
    case 14:    // Ctrl-n
        scr_BufferScrollUpDown(1, 0);
        break;
    case 17:    // Ctrl-q
        scr_CheckAutoAway(TRUE);
        scr_RosterUnreadMessage(1); // next unread message
        break;
    case 20:    // Ctrl-t
        readline_transpose_chars();
        break;
    case 23:    // Ctrl-w
        readline_backward_kill_word();
        break;
    case 27:    // ESC
        scr_CheckAutoAway(TRUE);
        currentWindow = NULL;
        chatmode = FALSE;
        if (current_buddy)
          buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
        scr_RosterVisibility(1);
        update_chat_status_window(FALSE);
        top_panel(chatPanel);
        top_panel(inputPanel);
        update_panels();
        break;
    case 12:    // Ctrl-l
        scr_CheckAutoAway(TRUE);
        scr_Resize();
        redrawwin(stdscr);
        break;
    case KEY_RESIZE:
        scr_Resize();
        break;
    default:
        if (isprint(key)) {
          char tmpLine[INPUTLINE_LENGTH+1];

          // Check the line isn't too long
          if (strlen(inputLine) >= INPUTLINE_LENGTH)
            return 0;

          // Insert char
          strcpy(tmpLine, ptr_inputline);
          *ptr_inputline++ = key;
          strcpy(ptr_inputline, tmpLine);
          check_offset(1);
        } else {
          const gchar *boundcmd = isbound(key);
          if (boundcmd) {
            gchar *cmd = g_strdup_printf("/%s", boundcmd);
            scr_CheckAutoAway(TRUE);
            if (process_command(cmd))
              return 255;
            g_free(cmd);
          } else {
            scr_LogPrint(LPRINT_NORMAL, "Unknown key=%d", key);
            if (utf8_mode)
              scr_LogPrint(LPRINT_NORMAL,
                           "WARNING: UTF-8 not yet supported!");
          }
        }
  }

  if (completion_started && key != 9 && key != KEY_RESIZE)
    scr_end_current_completion();
  refresh_inputline();
  if (!update_roster)
    doupdate();
  return 0;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
