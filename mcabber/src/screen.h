#ifndef __SCREEN_H__
#define __SCREEN_H__ 1

#include <glib.h>

#if HAVE_NCURSESW_NCURSES_H
# include <ncursesw/ncurses.h>
# include <ncursesw/panel.h>
#elif HAVE_NCURSES_NCURSES_H
# include <ncurses/ncurses.h>
# include <ncurses/panel.h>
#else
# include <ncurses.h>
# include <panel.h>
#endif

#ifdef WITH_ASPELL
void spellcheck_init(void);
void spellcheck_deinit(void);
//static void spellcheck(char*, char*);
#endif

#include "hbuf.h"
#include "logprint.h"

#define INPUTLINE_LENGTH  1024

// Only used in screen.c; this is the maximum line number
// in a multi-line message.  Should be < 1000
// Note: message length is limited by the HBB_BLOCKSIZE size too
#define MULTILINE_MAX_LINE_NUMBER 299

// When chatstates are enabled, timeout (in seconds) before "composing"
// becomes "paused" because of user inactivity.
// Warning: setting this very low will cause more network traffic.
#define COMPOSING_TIMEOUT 6L

enum colors {
  COLOR_GENERAL = 3,
  COLOR_MSGOUT,
  COLOR_MSGHL,
  COLOR_STATUS,
  COLOR_ROSTER,
  COLOR_ROSTERSEL,
  COLOR_ROSTERSELNMSG,
  COLOR_ROSTERNMSG,
  COLOR_INFO,
  COLOR_MSGIN,
  COLOR_max
};

int COLOR_ATTRIB[COLOR_max];

extern int update_roster;

typedef struct {
  int value;
  int utf8;
  enum {
    MKEY_META = 1,
    MKEY_EQUIV,
    MKEY_CTRL_PGUP,
    MKEY_CTRL_PGDOWN,
    MKEY_SHIFT_PGUP,
    MKEY_SHIFT_PGDOWN,
    MKEY_CTRL_SHIFT_PGUP,
    MKEY_CTRL_SHIFT_PGDOWN,
    MKEY_CTRL_HOME,
    MKEY_CTRL_END,
    MKEY_CTRL_INS,
    MKEY_CTRL_DEL,
    MKEY_CTRL_SHIFT_HOME,
    MKEY_CTRL_SHIFT_END,
    MKEY_MOUSE
  } mcode;
} keycode;

typedef enum {
  MC_ALL,
  MC_PRESET,
  MC_OFF,
  MC_REMOVE
} muccoltype;

void scr_init_bindings(void);

void scr_Getch(keycode *kcode);
void process_key(keycode kcode);

void scr_InitLocaleCharSet(void);
void scr_InitCurses(void);
void scr_TerminateCurses(void);
void scr_DrawMainWindow(unsigned int fullinit);
void scr_DrawRoster(void);
void scr_UpdateMainStatus(int forceupdate);
void scr_UpdateChatStatus(int forceupdate);
void scr_RosterVisibility(int status);
void scr_WriteIncomingMessage(const char *jidfrom, const char *text,
                              time_t timestamp, guint prefix,
                              unsigned mucnicklen);
void scr_WriteOutgoingMessage(const char *jidto,   const char *text,
                              guint prefix);
void scr_ShowBuddyWindow(void);
int  scr_BuddyBufferExists(const char *jid);
void scr_UpdateBuddyWindow(void);
void scr_set_chatmode(int enable);
int  scr_get_chatmode(void);
void scr_set_multimode(int enable, char *subject);
int  scr_get_multimode(void);
void scr_setmsgflag_if_needed(const char *jid, int special);
void scr_append_multiline(const char *line);
const char *scr_get_multiline(void);
const char *scr_get_multimode_subj(void);

guint scr_getprefixwidth(void);
void  scr_line_prefix(hbb_line *line, char *prefix, guint preflen);

void scr_Beep(void);

bool Autoaway;

long int scr_GetAutoAwayTimeout(time_t now);
void scr_CheckAutoAway(int activity);

#if defined JEP0022 || defined JEP0085
long int scr_GetChatStatesTimeout(time_t now);
#endif
int chatstates_disabled;

// For commands...
void scr_RosterTop(void);
void scr_RosterBottom(void);
void scr_RosterUp(void);
void scr_RosterDown(void);
void scr_RosterPrevGroup(void);
void scr_RosterNextGroup(void);
void scr_RosterSearch(char *);
void scr_RosterJumpJid(char *);
void scr_RosterDisplay(const char *);
void scr_BufferTopBottom(int topbottom);
void scr_BufferClear(void);
void scr_BufferScrollLock(int lock);
void scr_BufferPurge(int, const char*);
void scr_BufferPurgeAll(int);
void scr_BufferSearch(int direction, const char *text);
void scr_BufferPercent(int pc);
void scr_BufferDate(time_t t);
void scr_BufferDump(const char *file);
void scr_RosterUnreadMessage(int);
void scr_RosterJumpAlternate(void);
void scr_BufferScrollUpDown(int updown, unsigned int nblines);
bool scr_RosterColor(const char *status, const char *wildcard,
                     const char *color);
void scr_RosterClearColor(void);
void scr_MucColor(const char *muc, muccoltype type);
void scr_MucNickColor(const char *nick, const char *color);
void scr_BufferList(void);

void readline_transpose_chars(void);
void readline_forward_kill_word(void);
void readline_backward_kill_word(void);
void readline_backward_word(void);
void readline_forward_word(void);
void readline_updowncase_word(int);
void readline_capitalize_word(void);
void readline_backward_char(void);
void readline_forward_char(void);
int  readline_accept_line(int down_history);
void readline_cancel_completion(void);
void readline_do_completion(void);
void readline_refresh_screen(void);
void readline_disable_chat_mode(guint show_roster);
void readline_hist_beginning_search_bwd(void);
void readline_hist_beginning_search_fwd(void);
void readline_hist_prev(void);
void readline_hist_next(void);
void readline_backward_kill_char(void);
void readline_forward_kill_char(void);
void readline_iline_start(void);
void readline_iline_end(void);
void readline_backward_kill_iline(void);
void readline_forward_kill_iline(void);
void readline_send_multiline(void);

#endif

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
