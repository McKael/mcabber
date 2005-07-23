#ifndef __SCREEN_H__
#define __SCREEN_H__ 1

#include <ncurses.h>
#include <glib.h>

#define COLOR_GENERAL   3
#define COLOR_NMSG      4
#define COLOR_BD_DESSEL 5
#define COLOR_BD_DES    6

#define LOG_WIN_HEIGHT  (5+2)
#define ROSTER_WIDTH    24
#define PREFIX_WIDTH    17
#define CHAT_WIN_HEIGHT (maxY-1-LOG_WIN_HEIGHT)

#define INPUTLINE_LENGTH  1024

// Only used in screen.c; this is the maximum line number
// in a multi-line message.  Should be < 1000
// Note: message length is limited by the HBB_BLOCKSIZE size too
#define MULTILINE_MAX_LINE_NUMBER 299

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
inline void scr_set_multimode(int enable);
inline int  scr_get_multimode();
void scr_append_multiline(const char *line);
inline const char *scr_get_multiline();
void scr_handle_sigint(void);

int scr_Getch(void);

int process_key(int);

void scr_CheckAutoAway(bool activity);

// For commands...
void scr_RosterTop(void);
void scr_RosterBottom(void);
void scr_RosterSearch(char *);
void scr_BufferTopBottom(int topbottom);
void scr_Clear(void);
void scr_RosterUnreadMessage(int);
void scr_RosterJumpAlternate(void);

#endif
