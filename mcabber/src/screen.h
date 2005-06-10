#ifndef __SCREEN_H__
#define __SCREEN_H__ 1

#include <ncurses.h>
#include <glib.h>

//#define COLOR_NMSG      1
#define COLOR_GENERAL   3
//#define COLOR_BD_CON    4
#define COLOR_NMSG      5
#define COLOR_BD_DESSEL 6
#define COLOR_BD_DES    7

#define LOG_WIN_HEIGHT  (5+2)
#define ROSTER_WIDTH    24
#define PREFIX_WIDTH    17
#define CHAT_WIN_HEIGHT (maxY-1-LOG_WIN_HEIGHT)

#define INPUTLINE_LENGTH  1024

extern int update_roster;

void scr_InitCurses(void);
void scr_DrawMainWindow(unsigned int fullinit);
void scr_DrawRoster(void);
void scr_TerminateCurses(void);
void scr_WriteIncomingMessage(const char *jidfrom, const char *text,
        time_t timestamp, guint prefix);
void scr_WriteOutgoingMessage(const char *jidto,   const char *text);
void scr_ShowBuddyWindow(void);
void scr_LogPrint(const char *fmt, ...);
inline void scr_set_chatmode(int enable);

WINDOW *scr_GetInputWindow(void);

int scr_Getch(void);

int process_key(int);

// For commands...
void scr_RosterTop(void);
void scr_RosterBottom(void);
void scr_BufferTop(void);
void scr_BufferBottom(void);
void scr_Clear(void);
void scr_RosterUnreadMessage(int);

#endif
