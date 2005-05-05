#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>
#include <time.h>
#include <ctype.h>
#include <locale.h>

#include "screen.h"
#include "hbuf.h"
#include "commands.h"
#include "compl.h"
#include "roster.h"
#include "parsecfg.h"
#include "utils.h"
#include "list.h"

#define window_entry(n) list_entry(n, window_entry_t, list)

inline void check_offset(int);

LIST_HEAD(window_list);

typedef struct _window_entry_t {
  WINDOW *win;
  PANEL  *panel;
  char   *name;
  GList  *hbuf;
  GList  *top; // If top is not specified (NULL), we'll display the last lines
  char    cleared; // For ex, user has issued a /clear command...
  struct list_head list;
} window_entry_t;


static WINDOW *rosterWnd, *chatWnd, *inputWnd;
static WINDOW *logWnd, *logWnd_border;
static PANEL *rosterPanel, *chatPanel, *inputPanel;
static PANEL *logPanel, *logPanel_border;
static int maxY, maxX;
static window_entry_t *currentWindow;

static int chatmode;
int update_roster;

static char       inputLine[INPUTLINE_LENGTH+1];
static char      *ptr_inputline;
static short int  inputline_offset;
static int    completion_started;
static GList *cmdhisto;
static GList *cmdhisto_cur;
static char   cmdhisto_backup[INPUTLINE_LENGTH+1];


/* Functions */

int scr_WindowWidth(WINDOW * win)
{
  int x, y;
  getmaxyx(win, y, x);
  return x;
}

void scr_clear_box(WINDOW *win, int y, int x, int height, int width, int Color)
{
  int i, j;

  wattrset(win, COLOR_PAIR(Color));
  for (i = 0; i < height; i++) {
    wmove(win, y + i, x);
    for (j = 0; j < width; j++)
      wprintw(win, " ");
  }
}

void scr_draw_box(WINDOW * win, int y, int x, int height, int width,
                  int Color, chtype box, chtype border)
{
  int i, j;

  wattrset(win, COLOR_PAIR(Color));
  for (i = 0; i < height; i++) {
    wmove(win, y + i, x);
    for (j = 0; j < width; j++)
      if (!i && !j)
	waddch(win, border | ACS_ULCORNER);
      else if (i == height - 1 && !j)
	waddch(win, border | ACS_LLCORNER);
      else if (!i && j == width - 1)
	waddch(win, box | ACS_URCORNER);
      else if (i == height - 1 && j == width - 1)
	waddch(win, box | ACS_LRCORNER);
      else if (!i)
	waddch(win, border | ACS_HLINE);
      else if (i == height - 1)
	waddch(win, box | ACS_HLINE);
      else if (!j)
	waddch(win, border | ACS_VLINE);
      else if (j == width - 1)
	waddch(win, box | ACS_VLINE);
      else
	waddch(win, box | ' ');
  }
}

int FindColor(char *name)
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

void ParseColors(void)
{
  char *colors[11] = {
    "", "",
    "borderlines",
    "jidonline",
    "newmsg",
    "jidofflineselected",
    "jidoffline",
    "text",
    NULL
  };

  char *tmp = malloc(1024);
  char *color1;
  char *background = cfg_read("color_background");
  char *backselected = cfg_read("color_backselected");
  int i = 0;

  while (colors[i]) {
    sprintf(tmp, "color_%s", colors[i]);
    color1 = cfg_read(tmp);

    switch (i + 1) {
    case 1:
      init_pair(1, COLOR_BLACK, COLOR_WHITE);
      break;
    case 2:
      init_pair(2, COLOR_WHITE, COLOR_BLACK);
      break;
    case 3:
      init_pair(3, FindColor(color1), FindColor(background));
      break;
    case 4:
      init_pair(4, FindColor(color1), FindColor(backselected));
      break;
    case 5:
      init_pair(5, FindColor(color1), FindColor(background));
      break;
    case 6:
      init_pair(6, FindColor(color1), FindColor(backselected));
      break;
    case 7:
      init_pair(7, FindColor(color1), FindColor(background));
      break;
    case 8:
      init_pair(8, FindColor(color1), FindColor(background));
      break;
    }
    i++;
  }
}


window_entry_t *scr_CreateBuddyPanel(const char *title, int dont_show)
{
  int x;
  int y;
  int lines;
  int cols;
  window_entry_t *tmp;
  
  do {
    tmp = calloc(1, sizeof(window_entry_t));
  } while (!tmp);

  // Dimensions
  x = ROSTER_WIDTH;
  y = 0;
  lines = CHAT_WIN_HEIGHT;
  cols = maxX - ROSTER_WIDTH;

  tmp->win = newwin(lines, cols, y, x);
  while (!tmp->win) {
    usleep(250);
    tmp->win = newwin(lines, cols, y, x);
  }
  wbkgd(tmp->win, COLOR_PAIR(COLOR_GENERAL));
  tmp->panel = new_panel(tmp->win);
  tmp->name = (char *) calloc(1, 96);
  strncpy(tmp->name, title, 96);

  if (!dont_show) {
    currentWindow = tmp;
  } else {
    if (currentWindow)
      top_panel(currentWindow->panel);
    else
      top_panel(chatPanel);
  }
  update_panels();

  hlog_read_history(title, &tmp->hbuf, maxX - scr_WindowWidth(rosterWnd) - 14);
  list_add_tail(&tmp->list, &window_list);

  return tmp;
}

window_entry_t *scr_SearchWindow(const char *winId)
{
  struct list_head *pos, *n;
  window_entry_t *search_entry = NULL;

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

//  scr_UpdateWindow()
// (Re-)Display the given chat window.
void scr_UpdateWindow(window_entry_t *win_entry)
{
  int n;
  int width;
  char **lines;
  GList *hbuf_head;

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
    if (*(lines+2*n)) {
      if (**(lines+2*n))
        wprintw(win_entry->win, "%s", *(lines+2*n));      // prefix
      else {
        wprintw(win_entry->win, "            ");
      }
      wprintw(win_entry->win, "%s", *(lines+2*n+1));      // line
      wclrtoeol(win_entry->win);
    } else {
      wclrtobot(win_entry->win);
      break;
    }
  }
  g_free(lines);
}

//  scr_ShowWindow()
// Display the chat window with the given identifier.
void scr_ShowWindow(const char *winId)
{
  window_entry_t *win_entry = scr_SearchWindow(winId);

  if (win_entry != NULL) {
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
    // doupdate();    (update_roster should be enough?)
  } else {
    top_panel(chatPanel);
    currentWindow = win_entry;  // == NULL  (current window empty)
  }

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
void scr_WriteInWindow(const char *winId, const char *text, int TimeStamp,
        const char *prefix, int force_show)
{
  char *fullprefix = NULL;
  window_entry_t *win_entry;
  int dont_show = FALSE;

  // Prepare the prefix
  if (prefix || TimeStamp) {
    if (!prefix)  prefix = "";
    fullprefix = calloc(1, strlen(prefix)+16);
    if (TimeStamp) {
      time_t now = time(NULL);
      strftime(fullprefix, 12, "[%H:%M] ", localtime(&now));
    } else {
      strcpy(fullprefix, "            ");
    }
    strcat(fullprefix, prefix);
  }

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

  hbuf_add_line(&win_entry->hbuf, text, fullprefix,
                maxX - scr_WindowWidth(rosterWnd) - 14);
  free(fullprefix);

  if (win_entry->cleared) {
    win_entry->cleared = 0; // The message must be displayed
    win_entry->top = g_list_last(win_entry->hbuf);
  }

  if (!dont_show) {
    // Show and refresh the window
    top_panel(win_entry->panel);
    scr_UpdateWindow(win_entry);
    top_panel(inputPanel);
    update_panels();
    doupdate();
  } else {
    roster_msg_setflag(winId, TRUE);
    update_roster = TRUE;
  }
}

void scr_InitCurses(void)
{
  initscr();
  noecho();
  raw();
  halfdelay(5);
  start_color();
  use_default_colors();

  ParseColors();

  getmaxyx(stdscr, maxY, maxX);
  if (maxY < LOG_WIN_HEIGHT+2)
    maxY = LOG_WIN_HEIGHT+2;
  inputLine[0] = 0;
  ptr_inputline = inputLine;

  setlocale(LC_CTYPE, "");

  return;
}

void scr_TerminateCurses(void)
{
  clear();
  refresh();
  endwin();
  return;
}

//  scr_DrawMainWindow()
// Set fullinit to TRUE to also create panels.  Set it to FALSE for a resize.
//
// I think it could be improved a _lot_ but I'm really not an ncurses
// expert... :-\   Mikael.
//
void scr_DrawMainWindow(unsigned int fullinit)
{
  if (fullinit) {
    /* Create windows */
    rosterWnd = newwin(CHAT_WIN_HEIGHT, ROSTER_WIDTH, 0, 0);
    chatWnd   = newwin(CHAT_WIN_HEIGHT, maxX - ROSTER_WIDTH, 0, ROSTER_WIDTH);
    logWnd_border = newwin(LOG_WIN_HEIGHT, maxX, CHAT_WIN_HEIGHT, 0);
    logWnd    = newwin(LOG_WIN_HEIGHT-2, maxX-2, CHAT_WIN_HEIGHT+1, 1);
    inputWnd  = newwin(1, maxX, maxY-1, 0);
    wbkgd(rosterWnd,      COLOR_PAIR(COLOR_GENERAL));
    wbkgd(chatWnd,        COLOR_PAIR(COLOR_GENERAL));
    wbkgd(logWnd_border,  COLOR_PAIR(COLOR_GENERAL));
    wbkgd(logWnd,         COLOR_PAIR(COLOR_GENERAL));
  } else {
    /* Resize windows */
    wresize(rosterWnd, CHAT_WIN_HEIGHT, ROSTER_WIDTH);
    wresize(chatWnd, CHAT_WIN_HEIGHT, maxX - ROSTER_WIDTH);

    wresize(logWnd_border, LOG_WIN_HEIGHT, maxX);
    wresize(logWnd, LOG_WIN_HEIGHT-2, maxX-2);
    mvwin(logWnd_border, CHAT_WIN_HEIGHT, 0);
    mvwin(logWnd, CHAT_WIN_HEIGHT+1, 1);

    wresize(inputWnd, 1, maxX);
    mvwin(inputWnd, maxY-1, 0);

    werase(chatWnd);
  }

  /* Draw/init windows */

  mvwprintw(chatWnd, 0, 0, "This is the status window");

  // - Draw/clear the log window
  scr_draw_box(logWnd_border, 0, 0, LOG_WIN_HEIGHT, maxX, COLOR_GENERAL, 0, 0);
  // Auto-scrolling in log window
  scrollok(logWnd, TRUE);


  if (fullinit) {
    // Enable keypad (+ special keys)
    keypad(inputWnd, TRUE);

    // Create panels
    rosterPanel = new_panel(rosterWnd);
    chatPanel   = new_panel(chatWnd);
    logPanel_border = new_panel(logWnd_border);
    logPanel    = new_panel(logWnd);
    inputPanel  = new_panel(inputWnd);
  } else {
    // Update panels
    replace_panel(rosterPanel, rosterWnd);
    replace_panel(chatPanel, chatWnd);
    replace_panel(logPanel, logWnd);
    replace_panel(logPanel_border, logWnd_border);
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
  if (maxY < LOG_WIN_HEIGHT+2)
    maxY = LOG_WIN_HEIGHT+2;
  // Make sure the cursor stays inside the window
  check_offset(0);

  // Resize windows and update panels
  scr_DrawMainWindow(FALSE);

  // Resize all buddy windows
  x = ROSTER_WIDTH;
  y = 0;
  lines = CHAT_WIN_HEIGHT;
  cols = maxX - ROSTER_WIDTH;

  list_for_each_safe(pos, n, &window_list) {
    search_entry = window_entry(pos);
    if (search_entry->win) {
      // Resize buddy window (no need to move it)
      wresize(search_entry->win, lines, cols);
      werase(search_entry->win);
      // If a panel exists, replace the old window with the new
      if (search_entry->panel) {
        replace_panel(search_entry->panel, search_entry->win);
      }
      // Redo line wrapping
      hbuf_rebuild(&search_entry->hbuf,
              maxX - scr_WindowWidth(rosterWnd) - 14);
    }
  }

  // Refresh current buddy window
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_DrawRoster()
// Actually, display the buddylist on the screen.
void scr_DrawRoster(void)
{
  static guint offset = 0;
  char name[ROSTER_WIDTH];
  int maxx, maxy;
  GList *buddy;
  int i, n;
  int rOffset;
  enum imstatus currentstatus = jb_getstatus();

  // We can reset update_roster
  update_roster = FALSE;

  getmaxyx(rosterWnd, maxy, maxx);
  maxx --;  // last char is for vertical border
  name[ROSTER_WIDTH-7] = 0;

  // cleanup of roster window
  werase(rosterWnd);
  // Redraw the vertical line (not very good...)
  wattrset(rosterWnd, COLOR_PAIR(COLOR_GENERAL));
  for (i=0 ; i < CHAT_WIN_HEIGHT ; i++)
    mvwaddch(rosterWnd, i, ROSTER_WIDTH-1, ACS_VLINE);

  // Leave now if buddylist is empty
  if (!buddylist) {
    offset = 0;
    update_panels();
    doupdate();
    return;
  }

  // Update offset if necessary
  i = g_list_position(buddylist, current_buddy);
  if (i == -1) { // This is bad
    scr_LogPrint("Doh! Can't find current selected buddy!!");
    update_panels();
    doupdate();
    return;
  } else if (i < offset) {
    offset = i;
  } else if (i+1 > offset + maxy) {
    offset = i + 1 - maxy;
  }

  buddy = buddylist;
  rOffset = offset;

  for (i=0; i<maxy && buddy; buddy = g_list_next(buddy)) {

    char status = '?';
    char pending = ' ';
    enum imstatus budstate;
    unsigned short ismsg = buddy_getflags(BUDDATA(buddy)) & ROSTER_FLAG_MSG;
    unsigned short isgrp = buddy_gettype(BUDDATA(buddy)) & ROSTER_TYPE_GROUP;
    unsigned short ishid = buddy_getflags(BUDDATA(buddy)) & ROSTER_FLAG_HIDE;

    if (rOffset > 0) {
      rOffset--;
      continue;
    }

    // Display message notice if there is a message flag, but not
    // for unfolded groups.
    if (ismsg && (!isgrp || ishid)) {
      pending = '#';
    }

    budstate = buddy_getstatus(BUDDATA(buddy));
    if (budstate >= 0 && budstate < imstatus_size && currentstatus != offline)
      status = imstatus2char[budstate];
    if (buddy == current_buddy) {
      wattrset(rosterWnd, COLOR_PAIR(COLOR_BD_DESSEL));
      // The 3 following lines aim to color the whole line
      wmove(rosterWnd, i, 0);
      for (n = 0; n < maxx; n++)
        waddch(rosterWnd, ' ');
    } else {
      if (pending == '#')
        wattrset(rosterWnd, COLOR_PAIR(COLOR_NMSG));
      else
        wattrset(rosterWnd, COLOR_PAIR(COLOR_BD_DES));
    }

    strncpy(name, buddy_getname(BUDDATA(buddy)), ROSTER_WIDTH-7);
    if (isgrp) {
      char *sep;
      if (ishid)
        sep = "+++";
      else
        sep = "---";
      mvwprintw(rosterWnd, i, 0, " %c%s %s", pending, sep, name);
    }
    else
      mvwprintw(rosterWnd, i, 0, " %c[%c] %s", pending, status, name);

    i++;
  }

  top_panel(inputPanel);
  update_panels();
  doupdate();
}

void scr_WriteMessage(const char *jid, const char *text, char *prefix)
{
  scr_WriteInWindow(jid, text, TRUE, prefix, FALSE);
}

void scr_WriteIncomingMessage(const char *jidfrom, const char *text)
{
  // FIXME expand tabs / filter out special chars...
  scr_WriteMessage(jidfrom, text, "<== ");
  update_panels();
  doupdate();
}

void scr_WriteOutgoingMessage(const char *jidto, const char *text)
{
  scr_WriteMessage(jidto, text, "--> ");
  scr_ShowWindow(jidto);
}

int scr_Getch(void)
{
  int ch;
  ch = wgetch(inputWnd);
  return ch;
}

WINDOW *scr_GetRosterWindow(void)
{
  return rosterWnd;
}

WINDOW *scr_GetStatusWindow(void)
{
  return chatWnd;
}

WINDOW *scr_GetInputWindow(void)
{
  return inputWnd;
}

//  scr_RosterTop()
// Go to the first buddy in the buddylist
void scr_RosterTop(void)
{
  enum imstatus prev_st = imstatus_size; // undef

  if (current_buddy) {
    prev_st = buddy_getstatus(BUDDATA(current_buddy));
    if (chatmode)
      buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
  }
  current_buddy = buddylist;
  if (chatmode && current_buddy)
    buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, TRUE);

  // We should rebuild the buddylist but not everytime
  // Here we check if we were locking a buddy who is actually offline,
  // and hide_offline_buddies is TRUE.  In which case we need to rebuild.
  if (current_buddy && prev_st == offline &&
          buddylist_get_hide_offline_buddies())
    buddylist_build();
  if (chatmode)
    scr_ShowBuddyWindow();
  update_roster = TRUE;
}

//  scr_RosterBottom()
// Go to the last buddy in the buddylist
void scr_RosterBottom(void)
{
  enum imstatus prev_st = imstatus_size; // undef

  if (current_buddy) {
    prev_st = buddy_getstatus(BUDDATA(current_buddy));
    if (chatmode)
      buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
  }
  current_buddy = g_list_last(buddylist);
  // Lock the buddy in the buddylist if we're in chat mode
  if (chatmode && current_buddy)
    buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, TRUE);

  // We should rebuild the buddylist but not everytime
  // Here we check if we were locking a buddy who is actually offline,
  // and hide_offline_buddies is TRUE.  In which case we need to rebuild.
  if (current_buddy && prev_st == offline &&
          buddylist_get_hide_offline_buddies())
    buddylist_build();

  if (chatmode)
    scr_ShowBuddyWindow();
  update_roster = TRUE;
}

//  scr_RosterUp()
// Go to the previous buddy in the buddylist
void scr_RosterUp(void)
{
  enum imstatus prev_st = imstatus_size; // undef

  if (current_buddy) {
    if (g_list_previous(current_buddy)) {
      prev_st = buddy_getstatus(BUDDATA(current_buddy));
      buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
      current_buddy = g_list_previous(current_buddy);
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
  }

  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterDown()
// Go to the next buddy in the buddylist
void scr_RosterDown(void)
{
  enum imstatus prev_st = imstatus_size; // undef

  if (current_buddy) {
    if (g_list_next(current_buddy)) {
      prev_st = buddy_getstatus(BUDDATA(current_buddy));
      buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
      current_buddy = g_list_next(current_buddy);
      if (chatmode)
        buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, TRUE);
      // We should rebuild the buddylist but not everytime
      // Here we check if we were locking a buddy who is actually offline,
      // and hide_offline_buddies is TRUE.  In which case we need to rebuild.
      if (prev_st == offline && buddylist_get_hide_offline_buddies())
        buddylist_build();
      update_roster = TRUE;
    }
  }

  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_ScrollUp()
// Scroll up the current buddy window, half a screen.
void scr_ScrollUp(void)
{
  const gchar *jid;
  window_entry_t *win_entry;
  int n, nblines;
  GList *hbuf_top;

  // Get win_entry
  if (!current_buddy)
    return;
  jid = CURRENT_JID;
  if (!jid)
    return;
  win_entry  = scr_SearchWindow(jid);
  if (!win_entry)
    return;

  // Scroll up half a screen (or less)
  nblines = CHAT_WIN_HEIGHT/2-1;
  hbuf_top = win_entry->top;
  if (!hbuf_top) {
    hbuf_top = g_list_last(win_entry->hbuf);
    if (!win_entry->cleared)
      nblines *= 3;
    else
      win_entry->cleared = FALSE;
  }

  n = 0;
  while (hbuf_top && n < nblines && g_list_previous(hbuf_top)) {
    hbuf_top = g_list_previous(hbuf_top);
    n++;
  }
  win_entry->top = hbuf_top;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_ScrollDown()
// Scroll down the current buddy window, half a screen.
void scr_ScrollDown(void)
{
  const gchar *jid;
  window_entry_t *win_entry;
  int n, nblines;
  GList *hbuf_top;

  // Get win_entry
  if (!current_buddy)
    return;
  jid = CURRENT_JID;
  if (!jid)
    return;
  win_entry  = scr_SearchWindow(jid);
  if (!win_entry)
    return;

  // Scroll down half a screen (or less)
  nblines = CHAT_WIN_HEIGHT/2-1;
  hbuf_top = win_entry->top;

  for (n=0 ; hbuf_top && n < nblines ; n++)
    hbuf_top = g_list_next(hbuf_top);
  win_entry->top = hbuf_top;
  // Check if we are at the bottom
  for (n=0 ; hbuf_top && n < CHAT_WIN_HEIGHT-1 ; n++)
    hbuf_top = g_list_next(hbuf_top);
  if (!hbuf_top)
    win_entry->top = NULL; // End reached

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_Clear()
// Clear the current buddy window (used for the /clear command)
void scr_Clear(void)
{
  const gchar *jid;
  window_entry_t *win_entry;

  // Get win_entry
  if (!current_buddy)
    return;
  jid = CURRENT_JID;
  if (!jid)
    return;
  win_entry  = scr_SearchWindow(jid);
  if (!win_entry)
    return;

  win_entry->cleared = TRUE;
  win_entry->top = NULL;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
  doupdate();
}

//  scr_LogPrint(...)
// Display a message in the log window.
void scr_LogPrint(const char *fmt, ...)
{
  time_t timestamp;
  char *buffer;
  va_list ap;

  do {
    buffer = (char *) calloc(1, 1024);
  } while (!buffer);

  timestamp = time(NULL);
  strftime(buffer, 64, "[%H:%M:%S] ", localtime(&timestamp));
  wprintw(logWnd, "\n%s", buffer);

  va_start(ap, fmt);
  vsnprintf(buffer, 1024, fmt, ap);
  va_end(ap);

  wprintw(logWnd, "%s", buffer);
  free(buffer);

  update_panels();
  doupdate();
}

//  scr_set_chatmode()
// Public fonction to (un)set chatmode...
inline void scr_set_chatmode(int enable)
{
  chatmode = enable;
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
const char *scr_cmdhisto_prev(char *mask, guint len)
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
const char *scr_cmdhisto_next(char *mask, guint len)
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

//  which_row()
// Tells which row our cursor is in, in the command line.
// -1 -> normal text
//  0 -> command
//  1 -> parameter 1 (etc.)
//  If > 0, then *p_row is set to the beginning of the row
int which_row(char **p_row)
{
  int row = -1;
  char *p;
  int quote = FALSE;

  // Not a command?
  if ((ptr_inputline == inputLine) || (inputLine[0] != '/'))
    return -1;

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
void scr_insert_text(const char *text)
{
  char tmpLine[INPUTLINE_LENGTH+1];
  int len = strlen(text);
  // Check the line isn't too long
  if (strlen(inputLine) + len >= INPUTLINE_LENGTH) {
    scr_LogPrint("Cannot insert text, line too long.");
    return;
  }

  strcpy(tmpLine, ptr_inputline);
  strcpy(ptr_inputline, text);    ptr_inputline += len;
  strcpy(ptr_inputline, tmpLine);
}

//  scr_handle_tab()
// Function called when tab is pressed.
// Initiate or continue a completion...
void scr_handle_tab(void)
{
  int nrow;
  char *row;
  const char *cchar;
  guint compl_categ;

  nrow = which_row(&row);

  // a) No completion if no leading slash ('cause not a command)
  // b) We can't have more than 2 parameters (we use 2 flags)
  if (nrow < 0 || nrow > 2) return;

  if (nrow == 0) {      // Command completion
    row = &inputLine[1];
    compl_categ = COMPL_CMD;
  } else {              // Other completion, depending on the command
    cmd *com = cmd_get(inputLine);
    if (!com || !row) {
      scr_LogPrint("I cannot complete that...");
      return;
    }
    compl_categ = com->completion_flags[nrow-1];
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

void scr_cancel_current_completion(void)
{
  char *c;
  guint back = cancel_completion();
  // Remove $back chars
  ptr_inputline -= back;
  c = ptr_inputline;
  for ( ; *c ; c++)
    *c = *(c+back);
}

void scr_end_current_completion(void)
{
  done_completion();
  completion_started = FALSE;
}

//  check_offset(int direction)
// Check inputline_offset value, and make sure the cursor is inside the
// screen.
inline void check_offset(int direction)
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

//  process_key(key)
// Handle the pressed key, in the command line (bottom).
int process_key(int key)
{
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
    switch(key) {
      case KEY_BACKSPACE:
          if (ptr_inputline != (char*)&inputLine) {
            char *c = --ptr_inputline;
            for ( ; *c ; c++)
              *c = *(c+1);
            check_offset(-1);
          }
          break;
      case KEY_DC:
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
      case '\n':  // Enter
          if (process_line(inputLine))
            return 255;
          // Add line to history
          scr_cmdhisto_addline(inputLine);
          cmdhisto_cur = NULL;
          // Reset the line
          ptr_inputline = inputLine;
          *ptr_inputline = 0;
          inputline_offset = 0;
          break;
      case KEY_UP:
          {
            const char *l = scr_cmdhisto_prev(inputLine,
                    ptr_inputline-inputLine);
            if (l) {
              strcpy(inputLine, l);
            }
          }
          break;
      case KEY_DOWN:
          {
            const char *l = scr_cmdhisto_next(inputLine,
                    ptr_inputline-inputLine);
            if (l) {
              strcpy(inputLine, l);
            }
          }
          break;
      case KEY_PPAGE:
          scr_RosterUp();
          break;
      case KEY_NPAGE:
          scr_RosterDown();
          break;
      case KEY_HOME:
      case 1:
          ptr_inputline = inputLine;
          inputline_offset = 0;
          break;
      case KEY_END:
      case 5:
          for (; *ptr_inputline; ptr_inputline++) ;
          check_offset(1);
          break;
      case 21:  // Ctrl-u
          strcpy(inputLine, ptr_inputline);
          ptr_inputline = inputLine;
          inputline_offset = 0;
          break;
      case KEY_EOL:
      case 11:  // Ctrl-k
          *ptr_inputline = 0;
          break;
      case 16:  // Ctrl-p
          scr_ScrollUp();
          break;
      case 14:  // Ctrl-n
          scr_ScrollDown();
          break;
      case 27:  // ESC
          currentWindow = NULL;
          chatmode = FALSE;
          if (current_buddy)
            buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
          top_panel(chatPanel);
          top_panel(inputPanel);
          update_panels();
          break;
      case 12:  // Ctrl-l
      case KEY_RESIZE:
          scr_Resize();
          break;
      default:
          scr_LogPrint("Unkown key=%d", key);
    }
  }
  if (completion_started && key != 9 && key != KEY_RESIZE)
    scr_end_current_completion();
  mvwprintw(inputWnd, 0,0, "%s", inputLine + inputline_offset);
  wclrtoeol(inputWnd);
  if (*ptr_inputline) {
    wmove(inputWnd, 0, ptr_inputline - (char*)&inputLine - inputline_offset);
  }
  if (!update_roster) {
    //update_panels();
    doupdate();
  }
  return 0;
}
