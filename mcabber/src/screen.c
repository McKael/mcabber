#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>
#include <time.h>
#include <ctype.h>
#include <locale.h>

#include "screen.h"
#include "utils.h"
#include "buddies.h"
#include "parsecfg.h"
#include "lang.h"
#include "list.h"

#define window_entry(n) list_entry(n, window_entry_t, list)

LIST_HEAD(window_list);

typedef struct _window_entry_t {
  WINDOW *win;
  PANEL *panel;
  char *name;
  int nlines;
  char **texto;
  int hidden_msg;
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

int scr_WindowHeight(WINDOW * win)
{
  int x, y;
  getmaxyx(win, y, x);
  return x;
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


window_entry_t *scr_CreatePanel(char *title, int x, int y, int lines,
				int cols, int dont_show)
{
  window_entry_t *tmp = calloc(1, sizeof(window_entry_t));

  tmp->win = newwin(lines, cols, y, x);
  tmp->panel = new_panel(tmp->win);
  tmp->name = (char *) calloc(1, 1024);
  strncpy(tmp->name, title, 1024);

  scr_draw_box(tmp->win, 0, 0, lines, cols, COLOR_GENERAL, 0, 0);
  //mvwprintw(tmp->win, 0, (cols - (2 + strlen(title))) / 2, " %s ", title);
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

void scr_RoolWindow(void)
{
}

window_entry_t *scr_SearchWindow(char *winId)
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

void scr_ShowWindow(char *winId)
{
  int n, width, i;
  window_entry_t *tmp = scr_SearchWindow(winId);
  if (tmp != NULL) {
    top_panel(tmp->panel);
    currentWindow = tmp;
    chatmode = TRUE;
    tmp->hidden_msg = FALSE;
    update_roster = TRUE;
    width = scr_WindowHeight(tmp->win);
    for (n = 0; n < tmp->nlines; n++) {
      mvwprintw(tmp->win, n + 1, 1, "");
      for (i = 0; i < width - 2; i++)
	waddch(tmp->win, ' ');
      mvwprintw(tmp->win, n + 1, 1, "%s", tmp->texto[n]);
    }
    //move(CHAT_WIN_HEIGHT - 1, maxX - 1);
    update_panels();
    doupdate();
  } else {
    top_panel(chatPanel);
    currentWindow = tmp;
  }
}

void scr_ShowBuddyWindow(void)
{
  buddy_entry_t *tmp = bud_SelectedInfo();
  if (tmp->jid != NULL)
    scr_ShowWindow(tmp->jid);
  top_panel(inputPanel);
}


void scr_WriteInWindow(char *winId, char *texto, int TimeStamp, int force_show)
{
  time_t ahora;
  int n;
  int i;
  int width;
  window_entry_t *tmp;
  int dont_show = FALSE;

  tmp = scr_SearchWindow(winId);

  if (!chatmode)
    dont_show = TRUE;
  else if ((!force_show) && ((!currentWindow || (currentWindow != tmp))))
    dont_show = TRUE;

  if (tmp == NULL) {
    tmp = scr_CreatePanel(winId, ROSTER_WEIGHT, 0, CHAT_WIN_HEIGHT,
                          maxX - ROSTER_WEIGHT, dont_show);
    tmp->texto = (char **) calloc((CHAT_WIN_HEIGHT+1) * 3, sizeof(char *));
    for (n = 0; n < CHAT_WIN_HEIGHT * 3; n++)
      tmp->texto[n] = (char *) calloc(1, 1024);

    if (TimeStamp) {
      ahora = time(NULL);
      strftime(tmp->texto[tmp->nlines], 1024, "[%H:%M] ",
	       localtime(&ahora));
      strcat(tmp->texto[tmp->nlines], texto);
    } else {
      sprintf(tmp->texto[tmp->nlines], "            %s", texto);
    }
    tmp->nlines++;
  } else {
    if (tmp->nlines < CHAT_WIN_HEIGHT - 2) {
      if (TimeStamp) {
	ahora = time(NULL);
	strftime(tmp->texto[tmp->nlines], 1024,
		 "[%H:%M] ", localtime(&ahora));
	strcat(tmp->texto[tmp->nlines], texto);
      } else {
	sprintf(tmp->texto[tmp->nlines], "            %s", texto);
      }
      tmp->nlines++;
    } else {
      for (n = 0; n < tmp->nlines; n++) {
	memset(tmp->texto[n], 0, 1024);
	strncpy(tmp->texto[n], tmp->texto[n + 1], 1024);
      }
      if (TimeStamp) {
	ahora = time(NULL);
	strftime(tmp->texto[tmp->nlines - 1], 1024,
		 "[%H:%M] ", localtime(&ahora));
	strcat(tmp->texto[tmp->nlines - 1], texto);
      } else {
	sprintf(tmp->texto[tmp->nlines - 1], "            %s", texto);
      }
    }
  }

  if (!dont_show) {
    top_panel(tmp->panel);
    width = scr_WindowHeight(tmp->win);
    for (n = 0; n < tmp->nlines; n++) {
      mvwprintw(tmp->win, n + 1, 1, "");
      for (i = 0; i < width - 2; i++)
        waddch(tmp->win, ' ');
      mvwprintw(tmp->win, n + 1, 1, "%s", tmp->texto[n]);
    }

    update_panels();
    doupdate();
  } else {
    tmp->hidden_msg = TRUE;
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

void scr_DrawMainWindow(void)
{
  /* Draw main panels */
  rosterWnd = newwin(CHAT_WIN_HEIGHT, ROSTER_WEIGHT, 0, 0);
  rosterPanel = new_panel(rosterWnd);
  scr_draw_box(rosterWnd, 0, 0, CHAT_WIN_HEIGHT, ROSTER_WEIGHT,
               COLOR_GENERAL, 0, 0);
  mvwprintw(rosterWnd, 0, (ROSTER_WEIGHT - strlen(i18n("Roster"))) / 2,
	    i18n("Roster"));

  chatWnd = newwin(CHAT_WIN_HEIGHT, maxX - ROSTER_WEIGHT, 0, ROSTER_WEIGHT);
  chatPanel = new_panel(chatWnd);
  scr_draw_box(chatWnd, 0, 0, CHAT_WIN_HEIGHT, maxX - ROSTER_WEIGHT,
               COLOR_GENERAL, 0, 0);
  mvwprintw(chatWnd, 1, 1, "This is the status window");

  logWnd_border = newwin(LOG_WIN_HEIGHT, maxX, CHAT_WIN_HEIGHT, 0);
  logPanel_border = new_panel(logWnd_border);
  scr_draw_box(logWnd_border, 0, 0, LOG_WIN_HEIGHT, maxX, COLOR_GENERAL, 0, 0);
  logWnd = derwin(logWnd_border, LOG_WIN_HEIGHT-2, maxX-2, 1, 1);
  logPanel = new_panel(logWnd);
  wbkgd(logWnd, COLOR_PAIR(COLOR_GENERAL));

  scrollok(logWnd,TRUE);

  inputWnd = newwin(1, maxX, maxY-1, 0);
  inputPanel = new_panel(inputWnd);

  bud_DrawRoster(rosterWnd);
  update_panels();
  doupdate();
  return;
}

void scr_TerminateCurses(void)
{
  clear();
  refresh();
  endwin();
  return;
}

void scr_WriteIncomingMessage(char *jidfrom, char *text)
{
  char **submsgs;
  int n, i;
  char *buffer = (char *) malloc(5 + strlen(text));

  sprintf(buffer, "<== %s", utf8_decode(text));

  submsgs =
      ut_SplitMessage(buffer, &n, maxX - scr_WindowHeight(rosterWnd) - 14);

  for (i = 0; i < n; i++) {
    if (i == 0)
      scr_WriteInWindow(jidfrom, submsgs[i], TRUE, FALSE);
    else
      scr_WriteInWindow(jidfrom, submsgs[i], FALSE, FALSE);
  }

  for (i = 0; i < n; i++)
    free(submsgs[i]);

  free(submsgs);
  free(buffer);

  top_panel(inputPanel);
  //wmove(inputWnd, 0, ptr_inputline - (char*)&inputLine);
  update_panels();
  doupdate();
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

//  scr_IsHiddenMessage(jid)
// Returns TRUE if there is a hidden message in the window
// for the jid contact.
int scr_IsHiddenMessage(char *jid) {
  window_entry_t *wintmp;

  wintmp = scr_SearchWindow(jid);
  if ((wintmp) && (wintmp->hidden_msg))
    return TRUE;

  return FALSE;
}

//  send_message(msg)
// Write the message in the buddy's window and send the message on
// the network.
void send_message(char *msg)
{
  char **submsgs;
  char *buffer = (char *) calloc(1, 24+strlen(msg));
  char *buffer2 = (char *) calloc(1, 1024);
  int n, i;
  buddy_entry_t *tmp = bud_SelectedInfo();

  scr_ShowWindow(tmp->jid);

  sprintf(buffer, "--> %s", msg);

  submsgs =
	ut_SplitMessage(buffer, &n,
			maxX - scr_WindowHeight(rosterWnd) - 14);
  for (i = 0; i < n; i++) {
    if (i == 0)
      scr_WriteInWindow(tmp->jid, submsgs[i], TRUE, TRUE);
    else
      scr_WriteInWindow(tmp->jid, submsgs[i], FALSE, TRUE);
  }

  for (i = 0; i < n; i++)
    free(submsgs[i]);
  free(submsgs);

  refresh();
  sprintf(buffer2, "%s@%s/%s", cfg_read("username"),
          cfg_read("server"), cfg_read("resource"));
  jb_send_msg(tmp->jid, utf8_encode(msg));
  free(buffer);
  free(buffer2);

  top_panel(inputPanel);
}

//  process_line(line)
// Process the line *line.  Note: if this isn't a command, this is a message
// and it is sent to the current buddy.
int process_line(char *line)
{
  if (*line != '/') {
    send_message(line);
    return 0;
  }
  if (!strcasecmp(line, "/quit")) {
    return 255;
  }
  // Commands handling
  // TODO
  // say, send_raw...

  scr_LogPrint("Unrecognised command, sorry.");
  return 0;
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
          bud_RosterUp();
          if (chatmode)
            scr_ShowBuddyWindow();
          break;
      case KEY_DOWN:
          bud_RosterDown();
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
