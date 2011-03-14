#ifndef __MCABBER_SCREEN_H__
#define __MCABBER_SCREEN_H__ 1

#include <glib.h>

#include <mcabber/config.h>

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

#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
void spellcheck_init(void);
void spellcheck_deinit(void);
//static void spellcheck(char*, char*);
#endif

#include <mcabber/hbuf.h>
#include <mcabber/logprint.h>
#include <mcabber/roster.h>

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
extern gboolean chatstates_disabled;
extern gboolean Autoaway;

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


void scr_write_incoming_message(const char *jidfrom, const char *text,
                                time_t timestamp, guint prefix,
                                unsigned mucnicklen);
void scr_write_outgoing_message(const char *jidto,   const char *text,
                                guint prefix, gpointer xep184);

void scr_getch(keycode *kcode);
void scr_process_key(keycode kcode);

void scr_init_bindings(void);
void scr_init_locale_charset(void);
void scr_init_curses(void);
void scr_terminate_curses(void);
gboolean scr_curses_status(void);
void scr_draw_main_window(unsigned int fullinit);
void scr_draw_roster(void);
void scr_update_main_status(int forceupdate);
void scr_update_chat_status(int forceupdate);
void scr_roster_visibility(int status);
void scr_remove_receipt_flag(const char *jidto, gpointer xep184);
void scr_show_buddy_window(void);
int  scr_buddy_buffer_exists(const char *jid);
void scr_update_buddy_window(void);
void scr_set_chatmode(int enable);
int  scr_get_chatmode(void);
void scr_set_multimode(int enable, char *subject);
int  scr_get_multimode(void);
void scr_setmsgflag_if_needed(const char *jid, int special);
void scr_setattentionflag_if_needed(const char *bjid, int special,
                                    guint value, enum setuiprio_ops action);
void scr_append_multiline(const char *line);
const char *scr_get_multiline(void);
const char *scr_get_multimode_subj(void);

guint scr_getprefixwidth(void);
guint scr_gettextwidth(void);
guint scr_gettextheight(void);
guint scr_getlogwinheight(void);
void  scr_line_prefix(hbb_line *line, char *prefix, guint preflen);

void scr_beep(void);
void scr_check_auto_away(int activity);


// For commands...
void scr_roster_top(void);
void scr_roster_bottom(void);
void scr_roster_up_down(int updown, unsigned int n);
void scr_roster_prev_group(void);
void scr_roster_next_group(void);
void scr_roster_search(char *);
void scr_roster_jump_jid(char *);
void scr_roster_jump_alternate(void);
void scr_roster_unread_message(int);
void scr_roster_display(const char *);

void scr_buffer_top_bottom(int topbottom);
void scr_buffer_clear(void);
void scr_buffer_scroll_lock(int lock);
void scr_buffer_purge(int, const char*);
void scr_buffer_purge_all(int);
void scr_buffer_search(int direction, const char *text);
void scr_buffer_percent(int pc);
void scr_buffer_date(time_t t);
void scr_buffer_dump(const char *file);
void scr_buffer_list(void);
void scr_buffer_scroll_up_down(int updown, unsigned int nblines);
void scr_buffer_readmark(gboolean action);

bool scr_roster_color(const char *status, const char *wildcard,
                      const char *color);
void scr_roster_clear_color(void);
void scr_muc_color(const char *muc, muccoltype type);
void scr_muc_nick_color(const char *nick, const char *color);

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
void readline_insert(const char *toinsert);


// For backward compatibility:

#define scr_WriteIncomingMessage    scr_write_incoming_message
#define scr_WriteOutgoingMessage    scr_write_outgoing_message

#endif

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
