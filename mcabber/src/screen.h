#ifndef __SCREEN_H__
#define __SCREEN_H__ 1

#include <ncurses.h>
#include <glib.h>

#include "logprint.h"

#define COLOR_GENERAL       3
#define COLOR_STATUS        4
#define COLOR_ROSTER        5
#define COLOR_ROSTERSEL     6
#define COLOR_ROSTERSELNMSG 7
#define COLOR_ROSTERNMSG    8

// Length of the timestamp & flag prefix in the chat buffer window
#define PREFIX_WIDTH    17

#define INPUTLINE_LENGTH  1024

// Only used in screen.c; this is the maximum line number
// in a multi-line message.  Should be < 1000
// Note: message length is limited by the HBB_BLOCKSIZE size too
#define MULTILINE_MAX_LINE_NUMBER 299

extern int update_roster;

void scr_InitCurses(void);
void scr_TerminateCurses(void);
void scr_DrawMainWindow(unsigned int fullinit);
void scr_DrawRoster(void);
void scr_UpdateMainStatus(void);
void scr_UpdateChatStatus(int forceupdate);
void scr_RosterVisibility(int status);
void scr_WriteIncomingMessage(const char *jidfrom, const char *text,
        time_t timestamp, guint prefix);
void scr_WriteOutgoingMessage(const char *jidto,   const char *text);
void scr_ShowBuddyWindow(void);
bool scr_BuddyBufferExists(const char *jid);
inline void scr_set_chatmode(int enable);
inline void scr_set_multimode(int enable);
inline int  scr_get_multimode();
void scr_setmsgflag_if_needed(const char *jid);
void scr_append_multiline(const char *line);
inline const char *scr_get_multiline();

inline void scr_Beep(void);
int scr_Getch(void);

int process_key(int);

void scr_CheckAutoAway(bool activity);

// For commands...
void scr_RosterTop(void);
void scr_RosterBottom(void);
void scr_RosterUp(void);
void scr_RosterDown(void);
void scr_RosterSearch(char *);
void scr_RosterJumpJid(char *);
void scr_BufferTopBottom(int topbottom);
void scr_BufferClear(void);
void scr_BufferSearch(int direction, const char *text);
void scr_BufferPercent(int pc);
void scr_BufferDate(time_t t);
void scr_RosterUnreadMessage(int);
void scr_RosterJumpAlternate(void);
void scr_BufferScrollUpDown(int updown, unsigned int nblines);

#endif

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
