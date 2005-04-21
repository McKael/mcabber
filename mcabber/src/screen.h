#ifndef __SCREEN_H__
#define __SCREEN_H__ 1

#include <ncurses.h>

#define COLOR_POPUP     1
#define COLOR_GENERAL   3
#define COLOR_BD_CONSEL 4
#define COLOR_BD_CON    5
#define COLOR_BD_DESSEL 6
#define COLOR_BD_DES    7

#define LOG_WIN_HEIGHT  (5+2)
#define ROSTER_WIDTH    24
#define CHAT_WIN_HEIGHT (maxY-1-LOG_WIN_HEIGHT)

#define INPUTLINE_LENGTH  1024

extern int update_roster;

void scr_InitCurses(void);
void scr_DrawMainWindow(void);
void scr_DrawRoster(void);
void scr_TerminateCurses(void);
void scr_WriteIncomingMessage(const char *jidfrom, const char *text);
void scr_WriteOutgoingMessage(const char *jidto,   const char *text);
void scr_ShowBuddyWindow(void);
void scr_LogPrint(const char *fmt, ...);

WINDOW *scr_GetInputWindow(void);

int scr_Getch(void);

int process_key(int);

// For commands...
void scr_RosterTop();
void scr_RosterBottom();

#endif
