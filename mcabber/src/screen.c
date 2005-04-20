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
#include "lang.h"
#include "utf8.h"
#include "utils.h"
#include "list.h"

#define window_entry(n) list_entry(n, window_entry_t, list)

LIST_HEAD(window_list);

typedef struct _window_entry_t {
  WINDOW *win;
  PANEL *panel;
  char *name;
  GList *hbuf;
  struct list_head list;
} window_entry_t;


/* Variables globales a SCREEN.C */
static WINDOW *rosterWnd, *chatWnd, *inputWnd;
static WINDOW *logWnd, *logWnd_border;
static PANEL *rosterPanel, *chatPanel, *inputPanel;
static PANEL *logPanel, *logPanel_border;
static int maxY, maxX;
static window_entry_t *currentWindow;

static int chatmode;
int update_roster;

static char inputLine[INPUTLINE_LENGTH+1];
static char *ptr_inputline;
static short int inputline_offset;


/* Funciones */

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
    "jidonlineselected",
    "jidonline",
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


window_entry_t *scr_CreatePanel(const char *title, int x, int y,
                                int lines, int cols, int dont_show)
{
  window_entry_t *tmp = calloc(1, sizeof(window_entry_t));

  tmp->win = newwin(lines, cols, y, x);
  tmp->panel = new_panel(tmp->win);
  tmp->name = (char *) calloc(1, 1024);
  strncpy(tmp->name, title, 1024);
  scr_clear_box(tmp->win, 0, 0, lines, cols, COLOR_GENERAL);

  if ((!dont_show)) {
    currentWindow = tmp;
  } else {
    if (currentWindow)
      top_panel(currentWindow->panel);
    else
      top_panel(chatPanel);
  }

  list_add_tail(&tmp->list, &window_list);
  update_panels();

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

void scr_UpdateWindow(window_entry_t *win_entry)
{
  int n;
  int width;
  char **lines;
  GList *hbuf_head;

  // We will show the last CHAT_WIN_HEIGHT lines.
  // Let's find out where it begins.
  win_entry->hbuf = g_list_last(win_entry->hbuf);
  hbuf_head = win_entry->hbuf;
  for (n=0; hbuf_head && n<(CHAT_WIN_HEIGHT-1) && g_list_previous(hbuf_head); n++)
    hbuf_head = g_list_previous(hbuf_head);

  // Get the last CHAT_WIN_HEIGHT lines.
  lines = hbuf_get_lines(hbuf_head, CHAT_WIN_HEIGHT);

  // Display these lines
  width = scr_WindowWidth(win_entry->win);
  wmove(win_entry->win, 0, 0);
  for (n = 0; n < CHAT_WIN_HEIGHT; n++) {
    int r = width;
    if (*(lines+2*n)) {
      if (**(lines+2*n))
        wprintw(win_entry->win, "%s", *(lines+2*n));      // prefix
      else {
        wprintw(win_entry->win, "            ");
        r -= 12;
      }
      wprintw(win_entry->win, "%s", *(lines+2*n+1));      // line
      // Calculate the number of blank characters to empty the line
      r -= strlen(*(lines+2*n)) + strlen(*(lines+2*n+1));
    }
    for ( ; r>0 ; r--) {
      wprintw(win_entry->win, " ");
    }
    //// wclrtoeol(win_entry->win);  does not work :(
  }
  g_free(lines);
}

void scr_ShowWindow(const char *winId)
{
  window_entry_t *win_entry = scr_SearchWindow(winId);

  if (win_entry != NULL) {
    top_panel(win_entry->panel);
    currentWindow = win_entry;
    chatmode = TRUE;
    roster_setflags(winId, ROSTER_FLAG_MSG, FALSE);
    update_roster = TRUE;

    // Refresh the window entry
    scr_UpdateWindow(win_entry);

    // Finished :)
    update_panels();
    doupdate();
  } else {
    top_panel(chatPanel);
    currentWindow = win_entry;  // == NULL  (current window empty)
  }
}

void scr_ShowBuddyWindow(void)
{
  const gchar *jid = CURRENT_JID;
  if (jid != NULL)
    scr_ShowWindow(jid);
  top_panel(inputPanel);
}


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
    win_entry = scr_CreatePanel(winId, ROSTER_WIDTH, 0, CHAT_WIN_HEIGHT,
                          maxX - ROSTER_WIDTH, dont_show);
  }

  hbuf_add_line(&win_entry->hbuf, text, fullprefix,
                maxX - scr_WindowWidth(rosterWnd) - 14);
  free(fullprefix);

  if (!dont_show) {
    // Show and refresh the window
    top_panel(win_entry->panel);
    scr_UpdateWindow(win_entry);
    update_panels();
    doupdate();
  } else {
    roster_setflags(winId, ROSTER_FLAG_MSG, TRUE);
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

void scr_DrawMainWindow(void)
{
  int l;

  /* Draw main panels */
  rosterWnd = newwin(CHAT_WIN_HEIGHT, ROSTER_WIDTH, 0, 0);
  rosterPanel = new_panel(rosterWnd);
  scr_clear_box(rosterWnd, 0, 0, CHAT_WIN_HEIGHT, ROSTER_WIDTH,
                COLOR_GENERAL);
  for (l=0 ; l < CHAT_WIN_HEIGHT ; l++)
    mvwaddch(rosterWnd, l, ROSTER_WIDTH-1, ACS_VLINE);

  chatWnd = newwin(CHAT_WIN_HEIGHT, maxX - ROSTER_WIDTH, 0, ROSTER_WIDTH);
  chatPanel = new_panel(chatWnd);
  scr_clear_box(chatWnd, 0, 0, CHAT_WIN_HEIGHT, maxX - ROSTER_WIDTH,
                COLOR_GENERAL);
  scrollok(chatWnd, TRUE);
  mvwprintw(chatWnd, 0, 0, "This is the status window");

  logWnd_border = newwin(LOG_WIN_HEIGHT, maxX, CHAT_WIN_HEIGHT, 0);
  logPanel_border = new_panel(logWnd_border);
  scr_draw_box(logWnd_border, 0, 0, LOG_WIN_HEIGHT, maxX, COLOR_GENERAL, 0, 0);
  logWnd = derwin(logWnd_border, LOG_WIN_HEIGHT-2, maxX-2, 1, 1);
  logPanel = new_panel(logWnd);
  wbkgd(logWnd, COLOR_PAIR(COLOR_GENERAL));

  scrollok(logWnd, TRUE);

  inputWnd = newwin(1, maxX, maxY-1, 0);
  inputPanel = new_panel(inputWnd);

  scr_DrawRoster();
  update_panels();
  doupdate();
  return;
}

void scr_DrawRoster(void)
{
  static guint offset = 0;
  char name[ROSTER_WIDTH];
  int maxx, maxy;
  GList *buddy;
  int i, n;
  int rOffset;

  getmaxyx(rosterWnd, maxy, maxx);
  maxx --;  // last char is for vertical border
  name[ROSTER_WIDTH-7] = 0;

  // cleanup of roster window
  wattrset(rosterWnd, COLOR_PAIR(COLOR_GENERAL));
  for (i = 0; i < maxy; i++) {
    mvwprintw(rosterWnd, i, 0, "");
    for (n = 0; n < maxx; n++)
      waddch(rosterWnd, ' ');
  }

  // Leave now if buddylist is empty
  if (!buddylist) {
    offset = 0;
    return;
  }

  // Update offset if necessary
  i = g_list_position(buddylist, current_buddy);
  if (i == -1) { // This is bad
    scr_LogPrint("Doh! Can't find current selected buddy!!");
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

    if (rOffset > 0) {
      rOffset--;
      continue;
    }

    if (buddy_getflags(BUDDATA(buddy)) & ROSTER_FLAG_MSG) {
      pending = '#';
    }

    budstate = buddy_getstatus(BUDDATA(buddy));
    if (budstate >= 0 && budstate < imstatus_size)
      status = imstatus2char[budstate];
    if (buddy == current_buddy) {
      wattrset(rosterWnd, COLOR_PAIR(COLOR_BD_DESSEL));
      // The 3 following lines aim to color the whole line
      wmove(rosterWnd, i, 0);
      for (n = 0; n < maxx; n++)
        waddch(rosterWnd, ' ');
    } else {
      wattrset(rosterWnd, COLOR_PAIR(COLOR_BD_DES));
    }

    strncpy(name, buddy_getname(BUDDATA(buddy)), ROSTER_WIDTH-7);
    // TODO: status is meaningless for groups:
    if (buddy_gettype(BUDDATA(buddy)) & ROSTER_TYPE_GROUP) status='G';
    mvwprintw(rosterWnd, i, 0, " %c[%c] %s", pending, status, name);

    i++;
  }

  update_panels();
  doupdate();
  update_roster = FALSE;
}

void scr_WriteMessage(const char *jid, const char *text, char *prefix)
{
  scr_WriteInWindow(jid, text, TRUE, prefix, FALSE);
}

void scr_WriteIncomingMessage(const char *jidfrom, const char *text)
{
  char *buffer = utf8_decode(text);
  // FIXME expand tabs / filter out special chars...
  scr_WriteMessage(jidfrom, buffer, "<== ");
  free(buffer);
  top_panel(inputPanel);
  update_panels();
  doupdate();
}

void scr_WriteOutgoingMessage(const char *jidto, const char *text)
{
  scr_WriteMessage(jidto, text, "--> ");
  scr_ShowWindow(jidto);
  top_panel(inputPanel);
}

int scr_Getch(void)
{
  int ch;
  // keypad(inputWnd, TRUE);
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

void scr_RosterUp()
{
  if (current_buddy) {
    if (g_list_previous(current_buddy)) {
      current_buddy = g_list_previous(current_buddy);
      scr_DrawRoster();
    }
  }
  // XXX We should rebuild the buddylist but perhaps not everytime?
}

void scr_RosterDown()
{
  if (current_buddy) {
    if (g_list_next(current_buddy)) {
      current_buddy = g_list_next(current_buddy);
      scr_DrawRoster();
    }
  }
  // XXX We should rebuild the buddylist but perhaps not everytime?
}

//  scr_LogPrint(...)
// Display a message in the log window.
void scr_LogPrint(const char *fmt, ...)
{
  time_t timestamp;
  char *buffer;
  va_list ap;

  buffer = (char *) calloc(1, 4096);

  timestamp = time(NULL);
  strftime(buffer, 64, "[%H:%M:%S] ", localtime(&timestamp));
  wprintw(logWnd, "\n%s", buffer);

  va_start(ap, fmt);
  vsnprintf(buffer, 4096, fmt, ap);
  va_end(ap);

  wprintw(logWnd, "%s", buffer);
  free(buffer);

  update_panels();
  doupdate();
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
      case 9:     // Tab
          scr_LogPrint("I'm unable to complete yet");
          break;
      case '\n':  // Enter
          chatmode = TRUE;
          if (inputLine[0] == 0) {
            scr_ShowBuddyWindow();
            break;
          }
          if (process_line(inputLine))
            return 255;
          ptr_inputline = inputLine;
          *ptr_inputline = 0;
          inputline_offset = 0;
          break;
      case KEY_UP:
          scr_RosterUp();
          if (chatmode)
            scr_ShowBuddyWindow();
          break;
      case KEY_DOWN:
          scr_RosterDown();
          if (chatmode)
            scr_ShowBuddyWindow();
          break;
      case KEY_PPAGE:
          scr_LogPrint("PageUp??");
          break;
      case KEY_NPAGE:
          scr_LogPrint("PageDown??");
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
          scr_LogPrint("Ctrl-p not yet implemented");
          break;
      case 14:  // Ctrl-n
          scr_LogPrint("Ctrl-n not yet implemented");
          break;
      case 27:  // ESC
          currentWindow = NULL;
          chatmode = FALSE;
          top_panel(chatPanel);
          top_panel(inputPanel);
          break;
      default:
          scr_LogPrint("Unkown key=%d", key);
    }
  }
  mvwprintw(inputWnd, 0,0, "%s", inputLine + inputline_offset);
  wclrtoeol(inputWnd);
  if (*ptr_inputline) {
    wmove(inputWnd, 0, ptr_inputline - (char*)&inputLine - inputline_offset);
  }
  update_panels();
  doupdate();
  return 0;
}
