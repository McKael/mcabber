#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>
#include <time.h>
#include <ctype.h>

#include "screen.h"
#include "utils.h"
#include "buddies.h"
#include "parsecfg.h"
#include "lang.h"
#include "server.h"

#include "list.h"

/* Definicion de tipos */
#define window_entry(n) list_entry(n, window_entry_t, list)

typedef struct _window_entry_t {
  WINDOW *win;
  PANEL *panel;
  char *name;
  int nlines;
  char **texto;
  struct list_head list;
} window_entry_t;

LIST_HEAD(window_list);

/* Variables globales a SCREEN.C */
static WINDOW *rosterWnd, *chatWnd, *inputWnd;
static WINDOW *logWnd, *logWnd_border;
static PANEL *rosterPanel, *chatPanel, *inputPanel;
static PANEL *logPanel, *logPanel_border;
static int maxY, maxX;
static window_entry_t *currentWindow;

char inputLine[INPUTLINE_LENGTH];
char *ptr_inputline;


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
  mvwprintw(tmp->win, 0, (cols - (2 + strlen(title))) / 2, " %s ", title);
  if (!dont_show) {
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


void scr_CreatePopup(char *title, char *texto, int corte, int type,
                     char *returnstring)
{
  WINDOW *popupWin;
  PANEL *popupPanel;

  int lineas = 0;
  int cols = 0;

  char **submsgs;
  int n = 0;
  int i;

  char *instr = (char *) calloc(1, 1024);

  /* fprintf(stderr, "\r\n%d", lineas); */

  submsgs = ut_SplitMessage(texto, &n, corte);

  switch (type) {
  case 1:
  case 0:
    lineas = n + 4;
    break;
  }

  cols = corte + 3;
  popupWin = newwin(lineas, cols, (maxY - lineas) / 2, (maxX - cols) / 2);
  popupPanel = new_panel(popupWin);

  /*ATENCION!!! Colorear el popup ??
     / box (popupWin, 0, 0); */
  scr_draw_box(popupWin, 0, 0, lineas, cols, COLOR_POPUP, 0, 0);
  mvwprintw(popupWin, 0, (cols - (2 + strlen(title))) / 2, " %s ", title);

  for (i = 0; i < n; i++)
    mvwprintw(popupWin, i + 1, 2, "%s", submsgs[i]);


  for (i = 0; i < n; i++)
    free(submsgs[i]);
  free(submsgs);

  switch (type) {
  case 0:
    mvwprintw(popupWin, n + 2,
	      (cols - (2 + strlen(i18n("Press any key")))) / 2,
	      i18n("Press any key"));
    update_panels();
    doupdate();
    getch();
    break;
  case 1:
    {
      char ch;
      int scroll = 0;
      int input_x = 0;

      wmove(popupWin, 3, 1);
      wrefresh(popupWin);
      keypad(popupWin, TRUE);
      while ((ch = getch()) != '\n') {
	switch (ch) {
	case 0x09:
	case KEY_UP:
	case KEY_DOWN:
	  break;
	case KEY_RIGHT:
	case KEY_LEFT:
	  break;
	case KEY_BACKSPACE:
	case 127:
	  if (input_x || scroll) {
	    /* wattrset (popupWin, 0); */
	    if (!input_x) {
	      scroll = scroll < cols - 3 ? 0 : scroll - (cols - 3);
	      wmove(popupWin, 3, 1);
	      for (i = 0; i < cols; i++)
		waddch
		    (popupWin,
		     instr
		     [scroll
		      + input_x + i] ? instr[scroll + input_x + i] : ' ');
	      input_x = strlen(instr) - scroll;
	    } else
	      input_x--;
	    instr[scroll + input_x] = '\0';
	    mvwaddch(popupWin, 3, input_x + 1, ' ');
	    wmove(popupWin, 3, input_x + 1);
	    wrefresh(popupWin);
	  }
	default:
	  if ( /*ch<0x100 && */ isprint(ch) || ch == 'ñ'
	      || ch == 'Ñ') {
	    if (scroll + input_x < 1024) {
	      instr[scroll + input_x] = ch;
	      instr[scroll + input_x + 1] = '\0';
	      if (input_x == cols - 3) {
		scroll++;
		wmove(popupWin, 3, 1);
		for (i = 0; i < cols - 3; i++)
		  waddch(popupWin, instr[scroll + i]);
	      } else {
		wmove(popupWin, 3, 1 + input_x++);
		waddch(popupWin, ch);
	      }
	      wrefresh(popupWin);
	    } else {
	      flash();
	    }
	  }
	}
      }
    }
    if (returnstring != NULL)
      strcpy(returnstring, instr);
    break;
  }

  del_panel(popupPanel);
  delwin(popupWin);
  update_panels();
  doupdate();
  free(instr);
  keypad(inputWnd, TRUE);
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
  }
}

void scr_ShowBuddyWindow(void)
{
  buddy_entry_t *tmp = bud_SelectedInfo();
  if (tmp->jid != NULL)
    scr_ShowWindow(tmp->jid);
}


void scr_WriteInWindow(char *winId, char *texto, int TimeStamp)
{
  time_t ahora;
  int n;
  int i;
  int width;
  window_entry_t *tmp;
  int dont_show = FALSE;


  tmp = scr_SearchWindow(winId);

  if (!currentWindow || (currentWindow != tmp))
    dont_show = TRUE;
  scr_LogPrint("dont_show=%d", dont_show);

  if (tmp == NULL) {
    tmp = scr_CreatePanel(winId, 20, 0, CHAT_WIN_HEIGHT, maxX - 20, dont_show);
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
  }
}

void scr_InitCurses(void)
{
  initscr();
  noecho();
  raw();
  //cbreak();
  start_color();
  use_default_colors();

  ParseColors();

  getmaxyx(stdscr, maxY, maxX);
  inputLine[0] = 0;
  ptr_inputline = inputLine;

  return;
}

void scr_DrawMainWindow(void)
{
  /* Draw main panels */
  rosterWnd = newwin(maxY-1, 20, 0, 0);
  rosterPanel = new_panel(rosterWnd);
  scr_draw_box(rosterWnd, 0, 0, maxY-1, 20, COLOR_GENERAL, 0, 0);
  mvwprintw(rosterWnd, 0, (20 - strlen(i18n("Roster"))) / 2,
	    i18n("Roster"));

  chatWnd = newwin(CHAT_WIN_HEIGHT, maxX - 20, 0, 20);
  chatPanel = new_panel(chatWnd);
  scr_draw_box(chatWnd, 0, 0, CHAT_WIN_HEIGHT, maxX - 20, COLOR_GENERAL, 0, 0);
  mvwprintw(chatWnd, 0,
	    ((maxX - 20) - strlen(i18n("Status Window"))) / 2,
	    i18n("Status Window"));
  //wbkgd(chatWnd, COLOR_PAIR(COLOR_GENERAL));

  logWnd_border = newwin(LOG_WIN_HEIGHT, maxX - 20, CHAT_WIN_HEIGHT, 20);
  logPanel_border = new_panel(logWnd_border);
  scr_draw_box(logWnd_border, 0, 0, LOG_WIN_HEIGHT, maxX - 20, COLOR_GENERAL, 0, 0);
//  mvwprintw(logWnd_border, 0,
//	    ((maxX - 20) - strlen(i18n("Log Window"))) / 2,
//	    i18n("Log Window"));
  //logWnd = newwin(LOG_WIN_HEIGHT - 2, maxX-20 - 2, CHAT_WIN_HEIGHT+1, 20+1);
  logWnd = derwin(logWnd_border, LOG_WIN_HEIGHT-2, maxX-20-2, 1, 1);
  logPanel = new_panel(logWnd);
  wbkgd(logWnd, COLOR_PAIR(COLOR_GENERAL));
  //wattrset(logWnd, COLOR_PAIR(COLOR_GENERAL));
  wprintw(logWnd, "Here we are\n");
  scr_LogPrint("Here we are :-)");

  scrollok(logWnd,TRUE);
  idlok(logWnd,TRUE);  // XXX Necessary?

  inputWnd = newwin(1, maxX, maxY-1, 0);
  inputPanel = new_panel(inputWnd);
  //wbkgd(inputWnd, COLOR_PAIR(COLOR_GENERAL));

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

  sprintf(buffer, "<<< %s", text);

  submsgs =
      ut_SplitMessage(buffer, &n, maxX - scr_WindowHeight(rosterWnd) - 20);

  for (i = 0; i < n; i++) {
    if (i == 0)
      scr_WriteInWindow(jidfrom, submsgs[i], TRUE);
    else
      scr_WriteInWindow(jidfrom, submsgs[i], FALSE);
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

void scr_WriteMessage(int sock)
{
  char **submsgs;
  int n, i;
  char *buffer = (char *) calloc(1, 1024);
  char *buffer2 = (char *) calloc(1, 1024);
  buddy_entry_t *tmp = bud_SelectedInfo();

  scr_ShowWindow(tmp->jid);

  ut_CenterMessage(i18n("write your message here"), 60, buffer2);

  scr_CreatePopup(tmp->jid, buffer2, 60, 1, buffer);

  if (strlen(buffer)) {
    sprintf(buffer2, ">>> %s", buffer);

    submsgs =
	ut_SplitMessage(buffer2, &n,
			maxX - scr_WindowHeight(rosterWnd) - 20);
    for (i = 0; i < n; i++) {
      if (i == 0)
	scr_WriteInWindow(tmp->jid, submsgs[i], TRUE);
      else
	scr_WriteInWindow(tmp->jid, submsgs[i], FALSE);
    }

    for (i = 0; i < n; i++)
      free(submsgs[i]);
    free(submsgs);

    move(CHAT_WIN_HEIGHT - 1, maxX - 1);
    refresh();
    sprintf(buffer2, "%s@%s/%s", cfg_read("username"),
	    cfg_read("server"), cfg_read("resource"));
    srv_sendtext(sock, tmp->jid, buffer, buffer2);
  }
  free(buffer);
  free(buffer2);
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
}


void send_message(int sock, char *msg)
{
  char **submsgs;
  char *buffer = (char *) calloc(1, 24+strlen(msg));
  char *buffer2 = (char *) calloc(1, 1024);
  int n, i;
  buddy_entry_t *tmp = bud_SelectedInfo();

  scr_ShowWindow(tmp->jid);

  sprintf(buffer, ">>> %s", msg);

  submsgs =
	ut_SplitMessage(buffer, &n,
			maxX - scr_WindowHeight(rosterWnd) - 20);
  for (i = 0; i < n; i++) {
    if (i == 0)
      scr_WriteInWindow(tmp->jid, submsgs[i], TRUE);
    else
      scr_WriteInWindow(tmp->jid, submsgs[i], FALSE);
  }

  for (i = 0; i < n; i++)
    free(submsgs[i]);
  free(submsgs);

  //move(CHAT_WIN_HEIGHT - 1, maxX - 1);
  refresh();
  sprintf(buffer2, "%s@%s/%s", cfg_read("username"),
          cfg_read("server"), cfg_read("resource"));
  srv_sendtext(sock, tmp->jid, msg, buffer2);
  free(buffer);
  free(buffer2);

  top_panel(inputPanel);
}

int process_line(char *line, int sock)
{
  if (*line == 0)      // XXX Simple checks should maybe be in process_key()
    return 0;
  if (*line != '/') {
    send_message(sock, line);
    return 0;
  }
  if (!strcasecmp(line, "/quit")) {
    return 255;
  }
  // Commands handling
  // TODO
  // say...

  scr_LogPrint("Unrecognised command, sorry.");
  return 0;
}

int process_key(int key, int sock)
{
  if (isprint(key)) {
    char tmpLine[INPUTLINE_LENGTH];
    strcpy(tmpLine, ptr_inputline);
    *ptr_inputline++ = key;
    strcpy(ptr_inputline, tmpLine);
  } else {
    switch(key) {
      case KEY_BACKSPACE:
          if (ptr_inputline != (char*)&inputLine) {
            *--ptr_inputline = 0;
          }
          break;
      case KEY_DC:
          if (*ptr_inputline)
            strcpy(ptr_inputline, ptr_inputline+1);
          break;
      case KEY_LEFT:
          if (ptr_inputline != (char*)&inputLine) {
            ptr_inputline--;
          }
          break;
      case KEY_RIGHT:
          if (*ptr_inputline)
            ptr_inputline++;
          break;
      case 9:     // Tab
          scr_LogPrint("I'm unable to complete yet");
          break;
      case '\n':  // Enter
          // XXX Test:
          if (process_line(inputLine, sock))
            return 255;
          ptr_inputline = inputLine;
          *ptr_inputline = 0;
          break;
      case KEY_UP:
          bud_RosterUp();
          scr_ShowBuddyWindow();
          top_panel(inputPanel);
          break;
      case KEY_DOWN:
          bud_RosterDown();
          scr_ShowBuddyWindow();
          top_panel(inputPanel);
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
          break;
      case KEY_END:
      case 5:
          for (; *ptr_inputline; ptr_inputline++) ;
          break;
      case 21:   // Ctrl-u
          strcpy(inputLine, ptr_inputline);
          ptr_inputline = inputLine;
          break;
      case KEY_EOL:
      case 11:   // Ctrl-k
          *ptr_inputline = 0;
          break;
      default:
          scr_LogPrint("Unkown key=%o", key);
    }
    //scr_LogPrint("[%02x]", key);
  }
  mvwprintw(inputWnd, 0,0, "%s", inputLine);
  wclrtoeol(inputWnd);
  if (*ptr_inputline) {
    wmove(inputWnd, 0, ptr_inputline - (char*)&inputLine);
  }
  update_panels();
  doupdate();
  return 0;
}
