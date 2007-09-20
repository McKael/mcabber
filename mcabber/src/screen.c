/*
 * screen.c     -- UI stuff
 *
 * Copyright (C) 2005-2007 Mikael Berthe <mikael@lilotux.net>
 * Parts of this file come from the Cabber project <cabber@ajmacias.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <locale.h>
#include <langinfo.h>
#include <config.h>
#include <assert.h>

#ifdef HAVE_ASPELL_H
# include <aspell.h>
#endif

#include "screen.h"
#include "utf8.h"
#include "hbuf.h"
#include "commands.h"
#include "compl.h"
#include "roster.h"
#include "histolog.h"
#include "settings.h"
#include "utils.h"

#define get_color(col)  (COLOR_PAIR(col)|COLOR_ATTRIB[col])

#define DEFAULT_LOG_WIN_HEIGHT (5+2)
#define DEFAULT_ROSTER_WIDTH    24
#define CHAT_WIN_HEIGHT (maxY-1-Log_Win_Height)

char *LocaleCharSet = "C";

static unsigned short int Log_Win_Height;
static unsigned short int Roster_Width;

static inline void check_offset(int);
static void scr_cancel_current_completion(void);
static void scr_end_current_completion(void);
static void scr_insert_text(const char*);
static void scr_handle_tab(void);

#ifdef HAVE_ASPELL_H
static void spellcheck(char *, char *);
#endif

static GHashTable *winbufhash;

typedef struct {
  GList  *hbuf;
  GList  *top;     // If top is NULL, we'll display the last lines
  char    cleared; // For ex, user has issued a /clear command...
  char    lock;
} buffdata;

typedef struct {
  WINDOW *win;
  PANEL  *panel;
  buffdata *bd;
} winbuf;

struct dimensions {
  int l;
  int c;
};

static WINDOW *rosterWnd, *chatWnd, *activechatWnd, *inputWnd, *logWnd;
static WINDOW *mainstatusWnd, *chatstatusWnd;
static PANEL *rosterPanel, *chatPanel, *activechatPanel, *inputPanel;
static PANEL *mainstatusPanel, *chatstatusPanel;
static PANEL *logPanel;
static int maxY, maxX;
static int prev_chatwidth;
static winbuf *statusWindow;
static winbuf *currentWindow;
static GList  *statushbuf;

static int roster_hidden;
static int chatmode;
static int multimode;
static char *multiline, *multimode_subj;
int update_roster;
int utf8_mode = 0;
static bool Curses;
static bool log_win_on_top;
static bool roster_win_on_right;
static time_t LastActivity;

static char       inputLine[INPUTLINE_LENGTH+1];
#ifdef HAVE_ASPELL_H
static char       maskLine[INPUTLINE_LENGTH+1];
#endif
static char      *ptr_inputline;
static short int  inputline_offset;
static int    completion_started;
static GList *cmdhisto;
static GList *cmdhisto_cur;
static guint  cmdhisto_nblines;
static char   cmdhisto_backup[INPUTLINE_LENGTH+1];

static int    chatstate; /* (0=active, 1=composing, 2=paused) */
static bool   lock_chatstate;
static time_t chatstate_timestamp;
int chatstates_disabled;

#define MAX_KEYSEQ_LENGTH 8

typedef struct {
  char *seqstr;
  guint mkeycode;
  gint  value;
} keyseq;

GSList *keyseqlist;
static void add_keyseq(char *seqstr, guint mkeycode, gint value);

void scr_WriteInWindow(const char *winId, const char *text, time_t timestamp,
                       unsigned int prefix_flags, int force_show,
                       unsigned mucnicklen);

#ifdef HAVE_ASPELL_H
#define ASPELLBADCHAR 5
AspellConfig *spell_config;
AspellSpeller *spell_checker;
#endif

typedef struct {
  char *status, *wildcard;
  int color;
  GPatternSpec *compiled;
} rostercolor;

static GSList *rostercolrules = NULL;

static GHashTable *muccolors = NULL, *nickcolors = NULL;

typedef struct {
  bool manual;//Manually set?
  int color;
} nickcolor;

static int nickcolcount = 0, *nickcols = NULL;
static muccoltype glob_muccol = MC_OFF;

/* Functions */

static int color_conv_table[] = {
  COLOR_BLACK,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW,
  COLOR_BLUE,
  COLOR_MAGENTA,
  COLOR_CYAN,
  COLOR_WHITE
};

static int color_conv_table_fg[] = {
  COLOR_BLACK_FG,
  COLOR_RED_FG,
  COLOR_GREEN_FG,
  COLOR_YELLOW_FG,
  COLOR_BLUE_FG,
  COLOR_MAGENTA_FG,
  COLOR_CYAN_FG,
  COLOR_WHITE_FG
};

static int color_to_color_fg(int color)
{
  unsigned i = 0;
  for ( ; i < sizeof color_conv_table / sizeof *color_conv_table; i++)
    if (color == color_conv_table[i])
      return color_conv_table_fg[i];
  return -1;
}

static int color_fg_to_color(int color)
{
  unsigned i = 0;
  if (color >= COLOR_BLACK_BOLD_FG)
    color -= COLOR_BLACK_BOLD_FG - COLOR_BLACK_FG;
  for ( ; i < sizeof color_conv_table_fg / sizeof *color_conv_table_fg; i++)
    if (color == color_conv_table_fg[i])
      return color_conv_table[i];
  return -1;
}

static int FindColorInternal(const char *name)
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

  return -2;
}

static int FindColor(const char *name)
{
  int result = FindColorInternal(name);
  if (result != -2)
    return result;

  scr_LogPrint(LPRINT_LOGNORM, "ERROR: Wrong color: %s", name);
  return -1;
}

static int get_user_color(const char *color)
{
  bool isbright = false;
  int cl;
  if (!strncmp(color, "bright", 6)) {
    isbright = true;
    color += 6;
  }
  cl = color_to_color_fg(FindColorInternal(color));
  if (cl < 0)
    return cl;
  if (isbright)
    cl += COLOR_BLACK_BOLD_FG - COLOR_BLACK_FG;
  return cl;
}

static void ensure_string_htable(GHashTable **table,
    GDestroyNotify value_destroy_func)
{
  if (*table)//Have it already
    return;
  *table = g_hash_table_new_full(g_str_hash, g_str_equal,
      g_free, value_destroy_func);
}

// Sets the coloring mode for given MUC
// The MUC room does not need to be in the roster at that time
// muc - the JID of room
// type - the new type
void scr_MucColor(const char *muc, muccoltype type)
{
  gchar *muclow = g_utf8_strdown(muc, -1);
  if (type == MC_REMOVE) {//Remove it
    if (strcmp(muc, "*")) {
      if (muccolors && g_hash_table_lookup(muccolors, muclow))
        g_hash_table_remove(muccolors, muclow);
    } else {
      scr_LogPrint(LPRINT_NORMAL, "Can not remove global coloring mode");
    }
    g_free(muclow);
  } else {//Add or overwrite
    if (strcmp(muc, "*")) {
      ensure_string_htable(&muccolors, g_free);
      muccoltype *value = g_new(muccoltype, 1);
      *value = type;
      g_hash_table_replace(muccolors, muclow, value);
    } else {
      glob_muccol = type;
      g_free(muclow);
    }
  }
  //Need to redraw?
  if (chatmode && ((buddy_search_jid(muc) == current_buddy) || !strcmp(muc, "*")))
    scr_UpdateBuddyWindow();
}

// Sets the color for nick in MUC
// If color is "-", the color is marked as automaticly assigned and is
// not used if the room is in the "preset" mode
void scr_MucNickColor(const char *nick, const char *color)
{
  char *snick = g_strdup_printf("<%s>", nick), *mnick = g_strdup_printf("*%s ", nick);
  bool need_update = false;
  if (!strcmp(color, "-")) {//Remove the color
    if (nickcolors) {
      nickcolor *nc = g_hash_table_lookup(nickcolors, snick);
      if (nc) {//Have this nick already
        nc->manual = false;
        nc = g_hash_table_lookup(nickcolors, mnick);
        assert(nc);//Must have both at the same time
        nc->manual = false;
      }// Else -> no color saved, nothing to delete
    }
    g_free(snick);//They are not saved in the hash
    g_free(mnick);
    need_update = true;
  } else if (!strcmp(color, "!")) {
    if (nickcolors) {
      g_free(g_hash_table_lookup(nickcolors, snick));
      g_hash_table_remove(nickcolors, snick);
      g_hash_table_remove(nickcolors, mnick);
    }
    g_free(snick);//They are not saved in the hash
    g_free(mnick);
    need_update = true;
  } else {
    int cl = get_user_color(color);
    if (cl < 0) {
      scr_LogPrint(LPRINT_NORMAL, "No such color name");
      g_free(snick);
      g_free(mnick);
    } else {
      nickcolor *nc = g_new(nickcolor, 1);
      ensure_string_htable(&nickcolors, NULL);
      nc->manual = true;
      nc->color = cl;
      //Free the struct, if any there already
      g_free(g_hash_table_lookup(nickcolors, mnick));
      //Save the new ones
      g_hash_table_replace(nickcolors, mnick, nc);
      g_hash_table_replace(nickcolors, snick, nc);
      need_update = true;
    }
  }
  if (need_update && chatmode &&
      (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_ROOM))
    scr_UpdateBuddyWindow();
}

static void free_rostercolrule(rostercolor *col)
{
  g_free(col->status);
  g_free(col->wildcard);
  g_pattern_spec_free(col->compiled);
  g_free(col);
}

// Removes all roster coloring rules
void scr_RosterClearColor(void)
{
  GSList *head;
  for (head = rostercolrules; head; head = g_slist_next(head)) {
    free_rostercolrule(head->data);
  }
  g_slist_free(rostercolrules);
  rostercolrules = NULL;
}

// Adds, modifies or removes roster coloring rule
// color set to "-" removes the rule,
// otherwise it is modified (if exists) or added
//
// Returns weather it was successfull (therefore the roster should be
// redrawed) or not. If it failed, for example because of invalid color
// name, it also prints the error.
bool scr_RosterColor(const char *status, const char *wildcard,
                     const char *color)
{
  GSList *head;
  GSList *found = NULL;
  for (head = rostercolrules; head; head = g_slist_next(head)) {
    rostercolor *rc = head->data;
    if ((!strcmp(status, rc->status)) && (!strcmp(wildcard, rc->wildcard))) {
      found = head;
      break;
    }
  }
  if (!strcmp(color,"-")) {//Delete the rule
    if (found) {
      free_rostercolrule(found->data);
      rostercolrules = g_slist_delete_link(rostercolrules, found);
      return TRUE;
    } else {
      scr_LogPrint(LPRINT_NORMAL, "No such color rule, nothing removed");
      return FALSE;
    }
  } else {
    int cl = get_user_color(color);
    if (cl < 0 ) {
      scr_LogPrint(LPRINT_NORMAL, "No such color name");
      return FALSE;
    }
    if (found) {
      rostercolor *rc = found->data;
      rc->color = cl;
    } else {
      rostercolor *rc = g_new(rostercolor, 1);
      rc->status = g_strdup(status);
      rc->wildcard = g_strdup(wildcard);
      rc->compiled = g_pattern_spec_new(wildcard);
      rc->color = cl;
      rostercolrules = g_slist_prepend(rostercolrules, rc);
    }
    return TRUE;
  }
}

static void ParseColors(void)
{
  const char *colors[] = {
    "", "",
    "general",
    "msgout",
    "msghl",
    "status",
    "roster",
    "rostersel",
    "rosterselmsg",
    "rosternewmsg",
    "info",
    "msgin",
    NULL
  };

  const char *color;
  const char *background   = settings_opt_get("color_background");
  const char *backselected = settings_opt_get("color_bgrostersel");
  const char *backstatus   = settings_opt_get("color_bgstatus");
  char *tmp;
  int i;

  // Initialize color attributes
  memset(COLOR_ATTRIB, 0, sizeof(COLOR_ATTRIB));

  // Default values
  if (!background)   background   = "black";
  if (!backselected) backselected = "cyan";
  if (!backstatus)   backstatus   = "blue";

  for (i=0; colors[i]; i++) {
    tmp = g_strdup_printf("color_%s", colors[i]);
    color = settings_opt_get(tmp);
    g_free(tmp);

    if (color) {
      if (!strncmp(color, "bright", 6)) {
        COLOR_ATTRIB[i+1] = A_BOLD;
        color += 6;
      }
    }

    switch (i + 1) {
      case 1:
          init_pair(1, COLOR_BLACK, COLOR_WHITE);
          break;
      case 2:
          init_pair(2, COLOR_WHITE, COLOR_BLACK);
          break;
      case COLOR_GENERAL:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_WHITE),
                    FindColor(background));
          break;
      case COLOR_MSGOUT:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_CYAN),
                    FindColor(background));
          break;
      case COLOR_MSGHL:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_YELLOW),
                    FindColor(background));
          break;
      case COLOR_STATUS:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_WHITE),
                    FindColor(backstatus));
          break;
      case COLOR_ROSTER:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_GREEN),
                    FindColor(background));
          break;
      case COLOR_ROSTERSEL:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_BLUE),
                    FindColor(backselected));
          break;
      case COLOR_ROSTERSELNMSG:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_RED),
                    FindColor(backselected));
          break;
      case COLOR_ROSTERNMSG:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_RED),
                    FindColor(background));
          break;
      case COLOR_INFO:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_WHITE),
                    FindColor(background));
          break;
      case COLOR_MSGIN:
          init_pair(i+1, ((color) ? FindColor(color) : COLOR_WHITE),
                    FindColor(background));
          break;
    }
  }
  for (i = COLOR_BLACK_FG; i < COLOR_max; i++) {
    init_pair(i, color_fg_to_color(i), FindColor(background));
    if (i >= COLOR_BLACK_BOLD_FG)
      COLOR_ATTRIB[i] = A_BOLD;
  }
  if (!nickcols) {
    char *ncolors = g_strdup(settings_opt_get("nick_colors"));
    if (ncolors) {
      char *ncolor_start, *ncolor_end;
      ncolor_start = ncolor_end = ncolors;

      while (*ncolor_end)
        ncolor_end++;

      while (ncolors < ncolor_end && *ncolors) {
        if ((*ncolors == ' ') || (*ncolors == '\t')) {
          ncolors++;
        } else {
          char *end = ncolors;
          int cl;
          while (*end && (*end != ' ') && (*end != '\t'))
            end++;
          *end = '\0';
          cl = get_user_color(ncolors);
          if (cl < 0) {
            scr_LogPrint(LPRINT_NORMAL, "Unknown color %s", ncolors);
          } else {
            nickcols = g_realloc(nickcols, (++nickcolcount) * sizeof *nickcols);
            nickcols[nickcolcount-1] = cl;
          }
          ncolors = end+1;
        }
      }
      g_free(ncolor_start);
    }
    if (!nickcols) {//Fallback to have something
      nickcolcount = 1;
      nickcols = g_new(int, 1);
      *nickcols = COLOR_GENERAL;
    }
  }
}

static void init_keycodes(void)
{
  add_keyseq("O5A", MKEY_EQUIV, 521); // Ctrl-Up
  add_keyseq("O5B", MKEY_EQUIV, 514); // Ctrl-Down
  add_keyseq("O5C", MKEY_EQUIV, 518); // Ctrl-Right
  add_keyseq("O5D", MKEY_EQUIV, 516); // Ctrl-Left
  add_keyseq("O6A", MKEY_EQUIV, 520); // Shift-Up
  add_keyseq("O6B", MKEY_EQUIV, 513); // Shift-Down
  add_keyseq("O6C", MKEY_EQUIV, 402); // Shift-Right
  add_keyseq("O6D", MKEY_EQUIV, 393); // Shift-Left
  add_keyseq("O2A", MKEY_EQUIV, 520); // Shift-Up
  add_keyseq("O2B", MKEY_EQUIV, 513); // Shift-Down
  add_keyseq("O2C", MKEY_EQUIV, 402); // Shift-Right
  add_keyseq("O2D", MKEY_EQUIV, 393); // Shift-Left
  add_keyseq("[5^", MKEY_CTRL_PGUP, 0);   // Ctrl-PageUp
  add_keyseq("[6^", MKEY_CTRL_PGDOWN, 0); // Ctrl-PageDown
  add_keyseq("[5@", MKEY_CTRL_SHIFT_PGUP, 0);   // Ctrl-Shift-PageUp
  add_keyseq("[6@", MKEY_CTRL_SHIFT_PGDOWN, 0); // Ctrl-Shift-PageDown
  add_keyseq("[7@", MKEY_CTRL_SHIFT_HOME, 0); // Ctrl-Shift-Home
  add_keyseq("[8@", MKEY_CTRL_SHIFT_END, 0);  // Ctrl-Shift-End
  add_keyseq("[8^", MKEY_CTRL_END, 0);  // Ctrl-End
  add_keyseq("[7^", MKEY_CTRL_HOME, 0); // Ctrl-Home
  add_keyseq("[2^", MKEY_CTRL_INS, 0);  // Ctrl-Insert
  add_keyseq("[3^", MKEY_CTRL_DEL, 0);  // Ctrl-Delete

  // Xterm
  add_keyseq("[1;5A", MKEY_EQUIV, 521); // Ctrl-Up
  add_keyseq("[1;5B", MKEY_EQUIV, 514); // Ctrl-Down
  add_keyseq("[1;5C", MKEY_EQUIV, 518); // Ctrl-Right
  add_keyseq("[1;5D", MKEY_EQUIV, 516); // Ctrl-Left
  add_keyseq("[1;6A", MKEY_EQUIV, 520); // Ctrl-Shift-Up
  add_keyseq("[1;6B", MKEY_EQUIV, 513); // Ctrl-Shift-Down
  add_keyseq("[1;6C", MKEY_EQUIV, 402); // Ctrl-Shift-Right
  add_keyseq("[1;6D", MKEY_EQUIV, 393); // Ctrl-Shift-Left
  add_keyseq("[1;6H", MKEY_CTRL_SHIFT_HOME, 0); // Ctrl-Shift-Home
  add_keyseq("[1;6F", MKEY_CTRL_SHIFT_END, 0);  // Ctrl-Shift-End
  add_keyseq("[1;2A", MKEY_EQUIV, 521); // Shift-Up
  add_keyseq("[1;2B", MKEY_EQUIV, 514); // Shift-Down
  add_keyseq("[5;5~", MKEY_CTRL_PGUP, 0);   // Ctrl-PageUp
  add_keyseq("[6;5~", MKEY_CTRL_PGDOWN, 0); // Ctrl-PageDown
  add_keyseq("[1;5F", MKEY_CTRL_END, 0);  // Ctrl-End
  add_keyseq("[1;5H", MKEY_CTRL_HOME, 0); // Ctrl-Home
  add_keyseq("[2;5~", MKEY_CTRL_INS, 0);  // Ctrl-Insert
  add_keyseq("[3;5~", MKEY_CTRL_DEL, 0);  // Ctrl-Delete

  // PuTTY
  add_keyseq("[A", MKEY_EQUIV, 521); // Ctrl-Up
  add_keyseq("[B", MKEY_EQUIV, 514); // Ctrl-Down
  add_keyseq("[C", MKEY_EQUIV, 518); // Ctrl-Right
  add_keyseq("[D", MKEY_EQUIV, 516); // Ctrl-Left

  // screen
  add_keyseq("Oa", MKEY_EQUIV, 521); // Ctrl-Up
  add_keyseq("Ob", MKEY_EQUIV, 514); // Ctrl-Down
  add_keyseq("Oc", MKEY_EQUIV, 518); // Ctrl-Right
  add_keyseq("Od", MKEY_EQUIV, 516); // Ctrl-Left
  add_keyseq("[a", MKEY_EQUIV, 520); // Shift-Up
  add_keyseq("[b", MKEY_EQUIV, 513); // Shift-Down
  add_keyseq("[c", MKEY_EQUIV, 402); // Shift-Right
  add_keyseq("[d", MKEY_EQUIV, 393); // Shift-Left
  add_keyseq("[5$", MKEY_SHIFT_PGUP, 0);   // Shift-PageUp
  add_keyseq("[6$", MKEY_SHIFT_PGDOWN, 0); // Shift-PageDown

  // VT100
  add_keyseq("[H", MKEY_EQUIV, KEY_HOME); // Home
  add_keyseq("[F", MKEY_EQUIV, KEY_END);  // End

  // Konsole Linux
  add_keyseq("[1~", MKEY_EQUIV, KEY_HOME); // Home
  add_keyseq("[4~", MKEY_EQUIV, KEY_END);  // End
}

//  scr_init_bindings()
// Create default key bindings
// Return 0 if error and 1 if none
void scr_init_bindings(void)
{
  GString *sbuf = g_string_new("");

  // Common backspace key codes: 8, 127
  settings_set(SETTINGS_TYPE_BINDING, "8", "iline char_bdel");    // Ctrl-h
  settings_set(SETTINGS_TYPE_BINDING, "127", "iline char_bdel");
  g_string_printf(sbuf, "%d", KEY_BACKSPACE);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "iline char_bdel");
  g_string_printf(sbuf, "%d", KEY_DC);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "iline char_fdel");
  g_string_printf(sbuf, "%d", KEY_LEFT);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "iline bchar");
  g_string_printf(sbuf, "%d", KEY_RIGHT);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "iline fchar");
  settings_set(SETTINGS_TYPE_BINDING, "7", "iline compl_cancel"); // Ctrl-g
  g_string_printf(sbuf, "%d", KEY_UP);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str,
               "iline hist_beginning_search_bwd");
  g_string_printf(sbuf, "%d", KEY_DOWN);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str,
               "iline hist_beginning_search_fwd");
  g_string_printf(sbuf, "%d", KEY_PPAGE);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "roster up");
  g_string_printf(sbuf, "%d", KEY_NPAGE);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "roster down");
  g_string_printf(sbuf, "%d", KEY_HOME);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "iline iline_start");
  settings_set(SETTINGS_TYPE_BINDING, "1", "iline iline_start");  // Ctrl-a
  g_string_printf(sbuf, "%d", KEY_END);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "iline iline_end");
  settings_set(SETTINGS_TYPE_BINDING, "5", "iline iline_end");    // Ctrl-e
  // Ctrl-o (accept-line-and-down-history):
  settings_set(SETTINGS_TYPE_BINDING, "15", "iline iline_accept_down_hist");
  settings_set(SETTINGS_TYPE_BINDING, "21", "iline iline_bdel");  // Ctrl-u
  g_string_printf(sbuf, "%d", KEY_EOL);
  settings_set(SETTINGS_TYPE_BINDING, sbuf->str, "iline iline_fdel");
  settings_set(SETTINGS_TYPE_BINDING, "11", "iline iline_fdel");  // Ctrl-k
  settings_set(SETTINGS_TYPE_BINDING, "16", "buffer up");         // Ctrl-p
  settings_set(SETTINGS_TYPE_BINDING, "14", "buffer down");       // Ctrl-n
  settings_set(SETTINGS_TYPE_BINDING, "20", "iline char_swap");   // Ctrl-t
  settings_set(SETTINGS_TYPE_BINDING, "23", "iline word_bdel");   // Ctrl-w
  settings_set(SETTINGS_TYPE_BINDING, "M98", "iline bword");      // Meta-b
  settings_set(SETTINGS_TYPE_BINDING, "M102", "iline fword");     // Meta-f
  settings_set(SETTINGS_TYPE_BINDING, "M100", "iline word_fdel"); // Meta-d
  // Ctrl-Left  (2 codes):
  settings_set(SETTINGS_TYPE_BINDING, "515", "iline bword");
  settings_set(SETTINGS_TYPE_BINDING, "516", "iline bword");
  // Ctrl-Right (2 codes):
  settings_set(SETTINGS_TYPE_BINDING, "517", "iline fword");
  settings_set(SETTINGS_TYPE_BINDING, "518", "iline fword");
  settings_set(SETTINGS_TYPE_BINDING, "12", "screen_refresh");    // Ctrl-l
  settings_set(SETTINGS_TYPE_BINDING, "27", "chat_disable");      // Esc
  settings_set(SETTINGS_TYPE_BINDING, "M27", "chat_disable");     // Esc-Esc
  settings_set(SETTINGS_TYPE_BINDING, "4", "iline send_multiline"); // Ctrl-d
  settings_set(SETTINGS_TYPE_BINDING, "M117", "iline word_upcase"); // Meta-u
  settings_set(SETTINGS_TYPE_BINDING, "M108", "iline word_downcase"); // Meta-l
  settings_set(SETTINGS_TYPE_BINDING, "M99", "iline word_capit"); // Meta-c

  settings_set(SETTINGS_TYPE_BINDING, "265", "help"); // Bind F1 to help...

  g_string_free(sbuf, TRUE);
}

//  is_speckey(key)
// Return TRUE if key is a special code, i.e. no char should be displayed on
// the screen.  It's not very nice, it's a workaround for the systems where
// isprint(KEY_PPAGE) returns TRUE...
static int is_speckey(int key)
{
  switch (key) {
    case 127:
    case 393:
    case 402:
    case KEY_BACKSPACE:
    case KEY_DC:
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_UP:
    case KEY_DOWN:
    case KEY_PPAGE:
    case KEY_NPAGE:
    case KEY_HOME:
    case KEY_END:
    case KEY_EOL:
        return TRUE;
  }

  // Fn keys
  if (key >= 265 && key < 265+12)
    return TRUE;

  // Special key combinations
  if (key >= 513 && key <= 521)
    return TRUE;

  return FALSE;
}

void scr_InitLocaleCharSet(void)
{
  setlocale(LC_CTYPE, "");
  LocaleCharSet = nl_langinfo(CODESET);
  utf8_mode = (strcmp(LocaleCharSet, "UTF-8") == 0);
}

void scr_InitCurses(void)
{
  /* Key sequences initialization */
  init_keycodes();

  initscr();
  raw();
  noecho();
  nonl();
  intrflush(stdscr, FALSE);
  start_color();
  use_default_colors();

  ParseColors();

  getmaxyx(stdscr, maxY, maxX);
  Log_Win_Height = DEFAULT_LOG_WIN_HEIGHT;
  // Note scr_DrawMainWindow() should be called early after scr_InitCurses()
  // to update Log_Win_Height and set max{X,Y}

  inputLine[0] = 0;
  ptr_inputline = inputLine;

  Curses = TRUE;
  return;
}

void scr_TerminateCurses(void)
{
  if (!Curses) return;
  clear();
  refresh();
  endwin();
  Curses = FALSE;
  return;
}

inline void scr_Beep(void)
{
  beep();
}

// This and following belongs to dynamic setting of time prefix
static const char *timeprefixes[] = {
  "%m-%d %H:%M ",
  "%H:%M ",
  " "
};

static const char *spectimeprefixes[] = {
  "%m-%d %H:%M:%S   ",
  "%H:%M:%S   ",
  "   "
};

static int timepreflengths[] = {
  17,
  11,
  6
};

static const char *gettprefix()
{
  return timeprefixes[settings_opt_get_int("time_prefix")];
}

static const char *getspectprefix()
{
  return spectimeprefixes[settings_opt_get_int("time_prefix")];
}

static unsigned getprefixwidth()
{
  return timepreflengths[settings_opt_get_int("time_prefix")];
}

//  scr_LogPrint(...)
// Display a message in the log window.
// This function will convert from UTF-8 unless the LPRINT_NOTUTF8 flag is set.
void scr_LogPrint(unsigned int flag, const char *fmt, ...)
{
  time_t timestamp;
  char strtimestamp[64];
  char *buffer, *btext;
  char *convbuf1 = NULL, *convbuf2 = NULL;
  va_list ap;

  if (!(flag & ~LPRINT_NOTUTF8)) return; // Shouldn't happen

  timestamp = time(NULL);
  strftime(strtimestamp, 48, "[%H:%M:%S]", localtime(&timestamp));
  va_start(ap, fmt);
  btext = g_strdup_vprintf(fmt, ap);
  va_end(ap);

  if (flag & LPRINT_NORMAL) {
    char *buffer_locale;
    char *buf_specialwindow;

    buffer = g_strdup_printf("%s %s", strtimestamp, btext);

    // Convert buffer to current locale for wprintw()
    if (!(flag & LPRINT_NOTUTF8))
      buffer_locale = convbuf1 = from_utf8(buffer);
    else
      buffer_locale = buffer;

    if (!buffer_locale) {
      wprintw(logWnd,
              "\n%s*Error: cannot convert string to locale.", strtimestamp);
      update_panels();
      g_free(buffer);
      g_free(btext);
      return;
    }

    // For the special status buffer, we need utf-8, but without the timestamp
    if (flag & LPRINT_NOTUTF8)
      buf_specialwindow = convbuf2 = to_utf8(btext);
    else
      buf_specialwindow = btext;

    if (Curses) {
      wprintw(logWnd, "\n%s", buffer_locale);
      update_panels();
      scr_WriteInWindow(NULL, buf_specialwindow, timestamp,
                        HBB_PREFIX_SPECIAL, FALSE, 0);
    } else {
      printf("%s\n", buffer_locale);
      // ncurses are not initialized yet, so we call directly hbuf routine
      hbuf_add_line(&statushbuf, buf_specialwindow, timestamp,
        HBB_PREFIX_SPECIAL, 0, 0, 0);
    }

    g_free(convbuf1);
    g_free(convbuf2);
    g_free(buffer);
  }

  if (flag & (LPRINT_LOG|LPRINT_DEBUG)) {
    strftime(strtimestamp, 23, "[%Y-%m-%d %H:%M:%S]", localtime(&timestamp));
    buffer = g_strdup_printf("%s %s\n", strtimestamp, btext);
    ut_WriteLog(flag, buffer);
    g_free(buffer);
  }
  g_free(btext);
}

static winbuf *scr_SearchWindow(const char *winId, int special)
{
  char *id;
  winbuf *wbp;

  if (special)
    return statusWindow; // Only one special window atm.

  if (!winId)
    return NULL;

  id = g_strdup(winId);
  mc_strtolower(id);
  wbp = g_hash_table_lookup(winbufhash, id);
  g_free(id);
  return wbp;
}

int scr_BuddyBufferExists(const char *bjid)
{
  return (scr_SearchWindow(bjid, FALSE) != NULL);
}

//  scr_new_buddy(title, dontshow)
// Note: title (aka winId/jid) can be NULL for special buffers
static winbuf *scr_new_buddy(const char *title, int dont_show)
{
  winbuf *tmp;

  tmp = g_new0(winbuf, 1);

  tmp->win = activechatWnd;
  tmp->panel = activechatPanel;

  if (!dont_show) {
    currentWindow = tmp;
  } else {
    if (currentWindow)
      top_panel(currentWindow->panel);
    else
      top_panel(chatPanel);
  }
  update_panels();

  // If title is NULL, this is a special buffer
  if (title) {
    char *id;
    id = hlog_get_log_jid(title);
    if (id) {
      winbuf *wb = scr_SearchWindow(id, FALSE);
      if (!wb)
        wb = scr_new_buddy(id, TRUE);
      tmp->bd=wb->bd;
      g_free(id);
    } else {  // Load buddy history from file (if enabled)
      tmp->bd = g_new0(buffdata, 1);
      hlog_read_history(title, &tmp->bd->hbuf,
                        maxX - Roster_Width - getprefixwidth());
    }

    id = g_strdup(title);
    mc_strtolower(id);
    g_hash_table_insert(winbufhash, id, tmp);
  } else {
    tmp->bd = g_new0(buffdata, 1);
  }
  return tmp;
}

//  scr_UpdateWindow()
// (Re-)Display the given chat window.
static void scr_UpdateWindow(winbuf *win_entry)
{
  int n;
  int width;
  hbb_line **lines, *line;
  GList *hbuf_head;
  char date[64];
  int color;

  width = getmaxx(win_entry->win);

  // Should the window be empty?
  if (win_entry->bd->cleared) {
    werase(win_entry->win);
    return;
  }

  // win_entry->bd->top is the top message of the screen.  If it set to NULL,
  // we are displaying the last messages.

  // We will show the last CHAT_WIN_HEIGHT lines.
  // Let's find out where it begins.
  if (!win_entry->bd->top || (g_list_position(g_list_first(win_entry->bd->hbuf),
                                              win_entry->bd->top) == -1)) {
    // Move up CHAT_WIN_HEIGHT lines
    win_entry->bd->hbuf = g_list_last(win_entry->bd->hbuf);
    hbuf_head = win_entry->bd->hbuf;
    win_entry->bd->top = NULL; // (Just to make sure)
    n = 0;
    while (hbuf_head && (n < CHAT_WIN_HEIGHT-1) && g_list_previous(hbuf_head)) {
      hbuf_head = g_list_previous(hbuf_head);
      n++;
    }
    // If the buffer is locked, remember current "top" line for the next time.
    if (win_entry->bd->lock)
      win_entry->bd->top = hbuf_head;
  } else
    hbuf_head = win_entry->bd->top;

  // Get the last CHAT_WIN_HEIGHT lines.
  lines = hbuf_get_lines(hbuf_head, CHAT_WIN_HEIGHT);

  // Display these lines
  for (n = 0; n < CHAT_WIN_HEIGHT; n++) {
    wmove(win_entry->win, n, 0);
    line = *(lines+n);
    if (line) {
      if (line->flags & HBB_PREFIX_HLIGHT_OUT)
        color = COLOR_MSGOUT;
      else if (line->flags & HBB_PREFIX_HLIGHT)
        color = COLOR_MSGHL;
      else if (line->flags & HBB_PREFIX_INFO)
        color = COLOR_INFO;
      else if (line->flags & HBB_PREFIX_IN)
        color = COLOR_MSGIN;
      else
        color = COLOR_GENERAL;

      if (color != COLOR_GENERAL)
        wattrset(win_entry->win, get_color(color));

      if (line->timestamp &&
          !(line->flags & (HBB_PREFIX_SPECIAL|HBB_PREFIX_CONT))) {
        strftime(date, 30, gettprefix(), localtime(&line->timestamp));
      } else
        strcpy(date, "           ");
      if (!(line->flags & HBB_PREFIX_CONT)) {
        if (line->flags & HBB_PREFIX_INFO) {
          char dir = '*';
          if (line->flags & HBB_PREFIX_IN)
            dir = '<';
          else if (line->flags & HBB_PREFIX_OUT)
            dir = '>';
          wprintw(win_entry->win, "%s*%c* ", date, dir);
        } else if (line->flags & HBB_PREFIX_ERR) {
          char dir = '#';
          if (line->flags & HBB_PREFIX_IN)
            dir = '<';
          else if (line->flags & HBB_PREFIX_OUT)
            dir = '>';
          wprintw(win_entry->win, "%s#%c# ", date, dir);
        } else if (line->flags & HBB_PREFIX_IN) {
          char cryptflag = line->flags & HBB_PREFIX_PGPCRYPT ? '~' : '=';
          wprintw(win_entry->win, "%s<%c= ", date, cryptflag);
        } else if (line->flags & HBB_PREFIX_OUT) {
          char cryptflag = line->flags & HBB_PREFIX_PGPCRYPT ? '~' : '-';
          wprintw(win_entry->win, "%s-%c> ", date, cryptflag);
        } else if (line->flags & HBB_PREFIX_SPECIAL) {
          strftime(date, 30, getspectprefix(), localtime(&line->timestamp));
          wprintw(win_entry->win, "%s   ", date);
        } else {
          wprintw(win_entry->win, "%s    ", date);
        }
      } else {
        wprintw(win_entry->win, "                " );
      }

      // Make sure we are at the right position
      wmove(win_entry->win, n, getprefixwidth()-1);

      //The MUC nick - overwrite with propper color
      if (line->mucnicklen) {
        //Store the char after the nick
        char tmp = line->text[line->mucnicklen];
        muccoltype type = glob_muccol, *typetmp;
        //Terminate the string after the nick
        line->text[line->mucnicklen] = '\0';
        char *mucjid = g_utf8_strdown(CURRENT_JID, -1);
        if (muccolors) {
          typetmp = g_hash_table_lookup(muccolors, mucjid);
          if (typetmp)
            type = *typetmp;
        }
        g_free(mucjid);
        nickcolor *actual = NULL;
        // Need to generate some random color?
        if ((type == MC_ALL) && (!nickcolors ||
            !g_hash_table_lookup(nickcolors, line->text))) {
          ensure_string_htable(&nickcolors, NULL);
          char *snick = g_strdup(line->text), *mnick = g_strdup(line->text);
          nickcolor *nc = g_new(nickcolor, 1);
          nc->color = nickcols[random() % nickcolcount];
          nc->manual = false;
          *snick = '<';
          snick[strlen(snick)-1] = '>';
          *mnick = '*';
          mnick[strlen(mnick)-1] = ' ';
          //Insert them
          g_hash_table_insert(nickcolors, snick, nc);
          g_hash_table_insert(nickcolors, mnick, nc);
        }
        if (nickcolors)
          actual = g_hash_table_lookup(nickcolors, line->text);
        if (actual && ((type == MC_ALL) || (actual->manual))
            && (line->flags & HBB_PREFIX_IN) &&
           (!(line->flags & HBB_PREFIX_HLIGHT_OUT)))
          wattrset(win_entry->win, get_color(actual->color));
        wprintw(win_entry->win, "%s", line->text);
        //Return the char
        line->text[line->mucnicklen] = tmp;
        //Return the color back
        wattrset(win_entry->win, get_color(color));
      }

      wprintw(win_entry->win, "%s", line->text+line->mucnicklen); // Display text line
      wclrtoeol(win_entry->win);

      // Return the color back
      if (color != COLOR_GENERAL)
        wattrset(win_entry->win, get_color(COLOR_GENERAL));
      g_free(line->text);
      g_free(line);
    } else {
      wclrtobot(win_entry->win);
      break;
    }
  }
  g_free(lines);
}

static winbuf *scr_CreateWindow(const char *winId, int special, int dont_show)
{
  if (special) {
    if (!statusWindow) {
      statusWindow = scr_new_buddy(NULL, dont_show);
      statusWindow->bd->hbuf = statushbuf;
    }
    return statusWindow;
  } else {
    return scr_new_buddy(winId, dont_show);
  }
}

//  scr_ShowWindow()
// Display the chat window with the given identifier.
// "special" must be true if this is a special buffer window.
static void scr_ShowWindow(const char *winId, int special)
{
  winbuf *win_entry;

  win_entry = scr_SearchWindow(winId, special);

  if (!win_entry) {
    win_entry = scr_CreateWindow(winId, special, FALSE);
  }

  top_panel(win_entry->panel);
  currentWindow = win_entry;
  chatmode = TRUE;
  if (!win_entry->bd->lock)
    roster_msg_setflag(winId, special, FALSE);
  if (!special)
    roster_setflags(winId, ROSTER_FLAG_LOCK, TRUE);
  update_roster = TRUE;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();

  top_panel(inputPanel);
}

//  scr_ShowBuddyWindow()
// Display the chat window buffer for the current buddy.
void scr_ShowBuddyWindow(void)
{
  const gchar *bjid;

  if (!current_buddy) {
    bjid = NULL;
  } else {
    bjid = CURRENT_JID;
    if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL) {
      scr_ShowWindow(buddy_getname(BUDDATA(current_buddy)), TRUE);
      return;
    }
  }

  if (!bjid) {
    top_panel(chatPanel);
    top_panel(inputPanel);
    currentWindow = NULL;
    return;
  }

  scr_ShowWindow(bjid, FALSE);
}

//  scr_UpdateBuddyWindow()
// (Re)Display the current window.
// If chatmode is enabled, call scr_ShowBuddyWindow(),
// else display the chat window.
inline void scr_UpdateBuddyWindow(void)
{
  if (chatmode) {
    scr_ShowBuddyWindow();
    return;
  }

  top_panel(chatPanel);
  top_panel(inputPanel);
}

//  scr_WriteInWindow()
// Write some text in the winId window (this usually is a jid).
// Use winId == NULL for the special status buffer.
// Lines are splitted when they are too long to fit in the chat window.
// If this window doesn't exist, it is created.
void scr_WriteInWindow(const char *winId, const char *text, time_t timestamp,
                       unsigned int prefix_flags, int force_show,
                       unsigned mucnicklen)
{
  winbuf *win_entry;
  char *text_locale;
  int dont_show = FALSE;
  int special;
  guint num_history_blocks;
  bool setmsgflg = FALSE;
  char *nicktmp, *nicklocaltmp;

  // Look for the window entry.
  special = (winId == NULL);
  win_entry = scr_SearchWindow(winId, special);

  // Do we have to really show the window?
  if (!chatmode)
    dont_show = TRUE;
  else if ((!force_show) && ((!currentWindow || (currentWindow != win_entry))))
    dont_show = TRUE;

  // If the window entry doesn't exist yet, let's create it.
  if (!win_entry) {
    win_entry = scr_CreateWindow(winId, special, dont_show);
  }

  // The message must be displayed -> update top pointer
  if (win_entry->bd->cleared)
    win_entry->bd->top = g_list_last(win_entry->bd->hbuf);

  // Make sure we do not free the buffer while it's locked or when
  // top is set.
  if (win_entry->bd->lock || win_entry->bd->top)
    num_history_blocks = 0U;
  else
    num_history_blocks = get_max_history_blocks();

  text_locale = from_utf8(text);
  //Convert the nick alone and compute its length
  if (mucnicklen) {
    nicktmp = g_strndup(text, mucnicklen);
    nicklocaltmp = from_utf8(nicktmp);
    mucnicklen = strlen(nicklocaltmp);
    g_free(nicklocaltmp);
    g_free(nicktmp);
  }
  hbuf_add_line(&win_entry->bd->hbuf, text_locale, timestamp, prefix_flags,
                maxX - Roster_Width - getprefixwidth(), num_history_blocks, mucnicklen);
  g_free(text_locale);

  if (win_entry->bd->cleared) {
    win_entry->bd->cleared = FALSE;
    if (g_list_next(win_entry->bd->top))
      win_entry->bd->top = g_list_next(win_entry->bd->top);
  }

  // Make sure the last line appears in the window; update top if necessary
  if (!win_entry->bd->lock && win_entry->bd->top) {
    int dist;
    GList *first = g_list_first(win_entry->bd->hbuf);
    dist = g_list_position(first, g_list_last(win_entry->bd->hbuf)) -
           g_list_position(first, win_entry->bd->top);
    if (dist >= CHAT_WIN_HEIGHT)
      win_entry->bd->top = NULL;
  }

  if (!dont_show) {
    if (win_entry->bd->lock)
      setmsgflg = TRUE;
    // Show and refresh the window
    top_panel(win_entry->panel);
    scr_UpdateWindow(win_entry);
    top_panel(inputPanel);
    update_panels();
  } else if (!(prefix_flags & HBB_PREFIX_NOFLAG)) {
    setmsgflg = TRUE;
  }
  if (setmsgflg && !special) {
    if (special && !winId)
      winId = SPECIAL_BUFFER_STATUS_ID;
    roster_msg_setflag(winId, special, TRUE);
    update_roster = TRUE;
  }
}

//  scr_UpdateMainStatus()
// Redraw the main (bottom) status line.
void scr_UpdateMainStatus(int forceupdate)
{
  char *sm = from_utf8(jb_getstatusmsg());

  werase(mainstatusWnd);
  mvwprintw(mainstatusWnd, 0, 0, "%c[%c] %s",
            (unread_msg(NULL) ? '#' : ' '),
            imstatus2char[jb_getstatus()], (sm ? sm : ""));
  if (forceupdate) {
    top_panel(inputPanel);
    update_panels();
  }
  g_free(sm);
}

//  scr_DrawMainWindow()
// Set fullinit to TRUE to also create panels.  Set it to FALSE for a resize.
//
// I think it could be improved a _lot_ but I'm really not an ncurses
// expert... :-\   Mikael.
//
void scr_DrawMainWindow(unsigned int fullinit)
{
  int requested_size;
  gchar *ver, *message;
  int chat_y_pos, chatstatus_y_pos, log_y_pos;
  int roster_x_pos, chat_x_pos;

  Log_Win_Height = DEFAULT_LOG_WIN_HEIGHT;
  requested_size = settings_opt_get_int("log_win_height");
  if (requested_size > 0) {
    if (maxY > requested_size + 3)
      Log_Win_Height = requested_size + 2;
    else
      Log_Win_Height = ((maxY > 5) ? (maxY - 2) : 3);
  } else if (requested_size < 0) {
    Log_Win_Height = 3;
  }

  if (maxY < Log_Win_Height+2) {
    if (maxY < 5) {
      Log_Win_Height = 3;
      maxY = Log_Win_Height+2;
    } else {
      Log_Win_Height = maxY - 2;
    }
  }

  if (roster_hidden) {
    Roster_Width = 0;
  } else {
    requested_size = settings_opt_get_int("roster_width");
    if (requested_size > 1)
      Roster_Width = requested_size;
    else if (requested_size == 1)
      Roster_Width = 2;
    else
      Roster_Width = DEFAULT_ROSTER_WIDTH;
  }

  log_win_on_top = (settings_opt_get_int("log_win_on_top") == 1);
  roster_win_on_right = (settings_opt_get_int("roster_win_on_right") == 1);

  if (log_win_on_top) {
    chat_y_pos = Log_Win_Height-1;
    log_y_pos = 0;
    chatstatus_y_pos = Log_Win_Height-2;
  } else {
    chat_y_pos = 0;
    log_y_pos = CHAT_WIN_HEIGHT+1;
    chatstatus_y_pos = CHAT_WIN_HEIGHT;
  }

  if (roster_win_on_right) {
    roster_x_pos = maxX - Roster_Width;
    chat_x_pos = 0;
  } else {
    roster_x_pos = 0;
    chat_x_pos = Roster_Width;
  }

  if (fullinit) {
    if (!winbufhash)
      winbufhash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    /* Create windows */
    rosterWnd = newwin(CHAT_WIN_HEIGHT, Roster_Width, chat_y_pos, roster_x_pos);
    chatWnd   = newwin(CHAT_WIN_HEIGHT, maxX - Roster_Width, chat_y_pos,
                       chat_x_pos);
    activechatWnd = newwin(CHAT_WIN_HEIGHT, maxX - Roster_Width, chat_y_pos,
                           chat_x_pos);
    logWnd    = newwin(Log_Win_Height-2, maxX, log_y_pos, 0);
    chatstatusWnd = newwin(1, maxX, chatstatus_y_pos, 0);
    mainstatusWnd = newwin(1, maxX, maxY-2, 0);
    inputWnd  = newwin(1, maxX, maxY-1, 0);
    if (!rosterWnd || !chatWnd || !logWnd || !inputWnd) {
      scr_TerminateCurses();
      fprintf(stderr, "Cannot create windows!\n");
      exit(EXIT_FAILURE);
    }
    wbkgd(rosterWnd,      get_color(COLOR_GENERAL));
    wbkgd(chatWnd,        get_color(COLOR_GENERAL));
    wbkgd(activechatWnd,  get_color(COLOR_GENERAL));
    wbkgd(logWnd,         get_color(COLOR_GENERAL));
    wbkgd(chatstatusWnd,  get_color(COLOR_STATUS));
    wbkgd(mainstatusWnd,  get_color(COLOR_STATUS));
  } else {
    /* Resize/move windows */
    wresize(rosterWnd, CHAT_WIN_HEIGHT, Roster_Width);
    wresize(chatWnd, CHAT_WIN_HEIGHT, maxX - Roster_Width);
    wresize(logWnd, Log_Win_Height-2, maxX);

    mvwin(chatWnd, chat_y_pos, chat_x_pos);
    mvwin(rosterWnd, chat_y_pos, roster_x_pos);
    mvwin(logWnd, log_y_pos, 0);

    // Resize & move chat status window
    wresize(chatstatusWnd, 1, maxX);
    mvwin(chatstatusWnd, chatstatus_y_pos, 0);
    // Resize & move main status window
    wresize(mainstatusWnd, 1, maxX);
    mvwin(mainstatusWnd, maxY-2, 0);
    // Resize & move input line window
    wresize(inputWnd, 1, maxX);
    mvwin(inputWnd, maxY-1, 0);

    werase(chatWnd);
  }

  /* Draw/init windows */

  ver = mcabber_version();
  message = g_strdup_printf("MCabber version %s.\n", ver);
  mvwprintw(chatWnd, 0, 0, message);
  mvwprintw(chatWnd, 1, 0, "http://www.lilotux.net/~mikael/mcabber/");
  g_free(ver);
  g_free(message);

  // Auto-scrolling in log window
  scrollok(logWnd, TRUE);


  if (fullinit) {
    // Enable keypad (+ special keys)
    keypad(inputWnd, TRUE);
    nodelay(inputWnd, TRUE);

    // Create panels
    rosterPanel = new_panel(rosterWnd);
    chatPanel   = new_panel(chatWnd);
    activechatPanel = new_panel(activechatWnd);
    logPanel    = new_panel(logWnd);
    chatstatusPanel = new_panel(chatstatusWnd);
    mainstatusPanel = new_panel(mainstatusWnd);
    inputPanel  = new_panel(inputWnd);

    // Build the buddylist at least once, to make sure the special buffer
    // is added
    buddylist_build();

    // Init prev_chatwidth; this variable will be used to prevent us
    // from rewrapping buffers when the width doesn't change.
    prev_chatwidth = maxX - Roster_Width - getprefixwidth();
    // Wrap existing status buffer lines
    hbuf_rebuild(&statushbuf, prev_chatwidth);

#ifndef UNICODE
    if (utf8_mode)
      scr_LogPrint(LPRINT_NORMAL,
                   "WARNING: Compiled without full UTF-8 support!");
#endif
  } else {
    // Update panels
    replace_panel(rosterPanel, rosterWnd);
    replace_panel(chatPanel, chatWnd);
    replace_panel(logPanel, logWnd);
    replace_panel(chatstatusPanel, chatstatusWnd);
    replace_panel(mainstatusPanel, mainstatusWnd);
    replace_panel(inputPanel, inputWnd);
  }

  // We'll need to redraw the roster
  update_roster = TRUE;
  return;
}

static void resize_win_buffer(gpointer key, gpointer value, gpointer data)
{
  winbuf *wbp = value;
  struct dimensions *dim = data;
  int chat_x_pos, chat_y_pos;
  int new_chatwidth;

  if (!(wbp && wbp->win))
    return;

  if (log_win_on_top)
    chat_y_pos = Log_Win_Height-1;
  else
    chat_y_pos = 0;

  if (roster_win_on_right)
    chat_x_pos = 0;
  else
    chat_x_pos = Roster_Width;

  // Resize/move buddy window
  wresize(wbp->win, dim->l, dim->c);
  mvwin(wbp->win, chat_y_pos, chat_x_pos);
  werase(wbp->win);
  // If a panel exists, replace the old window with the new
  if (wbp->panel)
    replace_panel(wbp->panel, wbp->win);
  // Redo line wrapping
  wbp->bd->top = hbuf_previous_persistent(wbp->bd->top);

  new_chatwidth = maxX - Roster_Width - getprefixwidth();
  if (new_chatwidth != prev_chatwidth)
    hbuf_rebuild(&wbp->bd->hbuf, new_chatwidth);
}

//  scr_Resize()
// Function called when the window is resized.
// - Resize windows
// - Rewrap lines in each buddy buffer
void scr_Resize(void)
{
  struct dimensions dim;

  // First, update the global variables
  getmaxyx(stdscr, maxY, maxX);
  // scr_DrawMainWindow() will take care of maxY and Log_Win_Height

  // Make sure the cursor stays inside the window
  check_offset(0);

  // Resize windows and update panels
  scr_DrawMainWindow(FALSE);

  // Resize all buddy windows
  dim.l = CHAT_WIN_HEIGHT;
  dim.c = maxX - Roster_Width;
  if (dim.c < 1)
    dim.c = 1;

  // Resize all buffers
  g_hash_table_foreach(winbufhash, resize_win_buffer, &dim);

  // Resize/move special status buffer
  if (statusWindow)
    resize_win_buffer(NULL, statusWindow, &dim);

  // Update prev_chatwidth, now that all buffers have been resized
  prev_chatwidth = maxX - Roster_Width - getprefixwidth();

  // Refresh current buddy window
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_UpdateChatStatus(forceupdate)
// Redraw the buddy status bar.
// Set forceupdate to TRUE if update_panels() must be called.
void scr_UpdateChatStatus(int forceupdate)
{
  unsigned short btype, isgrp, ismuc, isspe;
  const char *fullname;
  const char *msg = NULL;
  char status;
  char *buf, *buf_locale;

  // Usually we need to update the bottom status line too,
  // at least to refresh the pending message flag.
  scr_UpdateMainStatus(FALSE);

  // Clear the line
  werase(chatstatusWnd);

  if (chatmode)
    wprintw(chatstatusWnd, "~");

  if (!current_buddy) {
    if (forceupdate) {
      update_panels();
    }
    return;
  }

  fullname = buddy_getname(BUDDATA(current_buddy));
  btype = buddy_gettype(BUDDATA(current_buddy));

  isgrp = btype & ROSTER_TYPE_GROUP;
  ismuc = btype & ROSTER_TYPE_ROOM;
  isspe = btype  & ROSTER_TYPE_SPECIAL;

  if (chatmode && !isgrp) {
    winbuf *win_entry;
    win_entry = scr_SearchWindow(buddy_getjid(BUDDATA(current_buddy)), isspe);
    if (win_entry && win_entry->bd->lock)
      mvwprintw(chatstatusWnd, 0, 0, "*");
  }

  if (isgrp || isspe) {
    buf_locale = from_utf8(fullname);
    if (isgrp)
      mvwprintw(chatstatusWnd, 0, 5, "Group: %s", buf_locale);
    else
      mvwprintw(chatstatusWnd, 0, 5, "Special buffer: %s", buf_locale);
    g_free(buf_locale);
    if (forceupdate) {
      update_panels();
    }
    return;
  }

  status = '?';

  if (ismuc) {
    if (buddy_getinsideroom(BUDDATA(current_buddy)))
      status = 'C';
    else
      status = 'x';
  } else if (jb_getstatus() != offline) {
    enum imstatus budstate;
    budstate = buddy_getstatus(BUDDATA(current_buddy), NULL);
    if (budstate < imstatus_size)
      status = imstatus2char[budstate];
  }

  // No status message for groups & MUC rooms
  if (!isgrp && !ismuc) {
    GSList *resources, *p_res;
    resources = buddy_getresources(BUDDATA(current_buddy));
    msg = buddy_getstatusmsg(BUDDATA(current_buddy),
                             resources ? resources->data : "");
    // Free the resources list data
    for (p_res = resources ; p_res ; p_res = g_slist_next(p_res))
      g_free(p_res->data);
    g_slist_free(resources);
  } else if (ismuc) {
    msg = buddy_gettopic(BUDDATA(current_buddy));
  }
  if (!msg)
    msg = "";

  buf = g_strdup_printf("[%c] Buddy: %s -- %s", status, fullname, msg);
  replace_nl_with_dots(buf);
  buf_locale = from_utf8(buf);
  mvwprintw(chatstatusWnd, 0, 1, "%s", buf_locale);
  g_free(buf_locale);
  g_free(buf);

  // Display chatstates of the contact, if available.
  if (btype & ROSTER_TYPE_USER) {
    char eventchar = 0;
    guint event;

    // We do not specify the resource here, so one of the resources with the
    // highest priority will be used.
    event = buddy_resource_getevents(BUDDATA(current_buddy), NULL);

    if (event == ROSTER_EVENT_ACTIVE)
      eventchar = 'A';
    else if (event == ROSTER_EVENT_COMPOSING)
      eventchar = 'C';
    else if (event == ROSTER_EVENT_PAUSED)
      eventchar = 'P';
    else if (event == ROSTER_EVENT_INACTIVE)
      eventchar = 'I';
    else if (event == ROSTER_EVENT_GONE)
      eventchar = 'G';

    if (eventchar)
      mvwprintw(chatstatusWnd, 0, maxX-3, "[%c]", eventchar);
  }


  if (forceupdate) {
    update_panels();
  }
}

//  scr_DrawRoster()
// Display the buddylist (not really the roster) on the screen
void scr_DrawRoster(void)
{
  static int offset = 0;
  char *name, *rline;
  int maxx, maxy;
  GList *buddy;
  int i, n;
  int rOffset;
  int cursor_backup;
  char status, pending;
  enum imstatus currentstatus = jb_getstatus();
  int x_pos;

  // We can reset update_roster
  update_roster = FALSE;

  getmaxyx(rosterWnd, maxy, maxx);
  maxx--;  // Last char is for vertical border

  cursor_backup = curs_set(0);

  if (!buddylist)
    offset = 0;
  else
    scr_UpdateChatStatus(FALSE);

  // Cleanup of roster window
  werase(rosterWnd);

  if (Roster_Width) {
    int line_x_pos = roster_win_on_right ? 0 : Roster_Width-1;
    // Redraw the vertical line (not very good...)
    wattrset(rosterWnd, get_color(COLOR_GENERAL));
    for (i=0 ; i < CHAT_WIN_HEIGHT ; i++)
      mvwaddch(rosterWnd, i, line_x_pos, ACS_VLINE);
  }

  // Leave now if buddylist is empty or the roster is hidden
  if (!buddylist || !Roster_Width) {
    update_panels();
    curs_set(cursor_backup);
    return;
  }

  // Update offset if necessary
  // a) Try to show as many buddylist items as possible
  i = g_list_length(buddylist) - maxy;
  if (i < 0)
    i = 0;
  if (i < offset)
    offset = i;
  // b) Make sure the current_buddy is visible
  i = g_list_position(buddylist, current_buddy);
  if (i == -1) { // This is bad
    scr_LogPrint(LPRINT_NORMAL, "Doh! Can't find current selected buddy!!");
    curs_set(cursor_backup);
    return;
  } else if (i < offset) {
    offset = i;
  } else if (i+1 > offset + maxy) {
    offset = i + 1 - maxy;
  }

  if (roster_win_on_right)
    x_pos = 1; // 1 char offset (vertical line)
  else
    x_pos = 0;

  name = g_new0(char, 4*Roster_Width);
  rline = g_new0(char, 4*Roster_Width+1);

  buddy = buddylist;
  rOffset = offset;

  for (i=0; i<maxy && buddy; buddy = g_list_next(buddy)) {
    unsigned short bflags, btype, ismsg, isgrp, ismuc, ishid, isspe;
    gchar *rline_locale;
    GSList *resources, *p_res;

    bflags = buddy_getflags(BUDDATA(buddy));
    btype = buddy_gettype(BUDDATA(buddy));

    ismsg = bflags & ROSTER_FLAG_MSG;
    ishid = bflags & ROSTER_FLAG_HIDE;
    isgrp = btype  & ROSTER_TYPE_GROUP;
    ismuc = btype  & ROSTER_TYPE_ROOM;
    isspe = btype  & ROSTER_TYPE_SPECIAL;

    if (rOffset > 0) {
      rOffset--;
      continue;
    }

    status = '?';
    pending = ' ';

    resources = buddy_getresources(BUDDATA(buddy));
    for (p_res = resources ; p_res ; p_res = g_slist_next(p_res)) {
      guint events = buddy_resource_getevents(BUDDATA(buddy),
                                              p_res ? p_res->data : "");
      if ((events & ROSTER_EVENT_PAUSED) && pending != '+')
        pending = '.';
      if (events & ROSTER_EVENT_COMPOSING)
        pending = '+';
      g_free(p_res->data);
    }
    g_slist_free(resources);

    // Display message notice if there is a message flag, but not
    // for unfolded groups.
    if (ismsg && (!isgrp || ishid)) {
      pending = '#';
    }

    if (ismuc) {
      if (buddy_getinsideroom(BUDDATA(buddy)))
        status = 'C';
      else
        status = 'x';
    } else if (currentstatus != offline) {
      enum imstatus budstate;
      budstate = buddy_getstatus(BUDDATA(buddy), NULL);
      if (budstate < imstatus_size)
        status = imstatus2char[budstate];
    }
    if (buddy == current_buddy) {
      if (pending == '#')
        wattrset(rosterWnd, get_color(COLOR_ROSTERSELNMSG));
      else
        wattrset(rosterWnd, get_color(COLOR_ROSTERSEL));
      // The 3 following lines aim at coloring the whole line
      wmove(rosterWnd, i, x_pos);
      for (n = 0; n < maxx; n++)
        waddch(rosterWnd, ' ');
    } else {
      if (pending == '#')
        wattrset(rosterWnd, get_color(COLOR_ROSTERNMSG));
      else {
        int color = get_color(COLOR_ROSTER);
        if ((!isspe) && (!isgrp)) {//Look for color rules
          GSList *head;
          const char *jid = buddy_getjid(BUDDATA(buddy));
          for (head = rostercolrules; head; head = g_slist_next(head)) {
            rostercolor *rc = head->data;
            if (g_pattern_match_string(rc->compiled, jid) &&
                (!strcmp("*", rc->status) || strchr(rc->status, status))) {
              color = get_color(rc->color);
              break;
            }
          }
        }
        wattrset(rosterWnd, color);
      }
    }

    if (Roster_Width > 7)
      g_utf8_strncpy(name, buddy_getname(BUDDATA(buddy)), Roster_Width-7);
    else
      name[0] = 0;

    if (isgrp) {
      char *sep;
      if (ishid)
        sep = "+++";
      else
        sep = "---";
      snprintf(rline, 4*Roster_Width, " %c%s %s", pending, sep, name);
    } else if (isspe) {
      snprintf(rline, 4*Roster_Width, " %c%s", pending, name);
    } else {
      char sepleft  = '[';
      char sepright = ']';
      if (btype & ROSTER_TYPE_USER) {
        guint subtype = buddy_getsubscription(BUDDATA(buddy));
        if (status == '_' && !(subtype & sub_to))
          status = '?';
        if (!(subtype & sub_from)) {
          sepleft  = '{';
          sepright = '}';
        }
      }

      snprintf(rline, 4*Roster_Width,
               " %c%c%c%c %s", pending, sepleft, status, sepright, name);
    }

    rline_locale = from_utf8(rline);
    mvwprintw(rosterWnd, i, x_pos, "%s", rline_locale);
    g_free(rline_locale);
    i++;
  }

  g_free(rline);
  g_free(name);
  top_panel(inputPanel);
  update_panels();
  curs_set(cursor_backup);
}

//  scr_RosterVisibility(status)
// Set the roster visibility:
// status=1   Show roster
// status=0   Hide roster
// status=-1  Toggle roster status
void scr_RosterVisibility(int status)
{
  int old_roster_status = roster_hidden;

  if (status > 0)
    roster_hidden = FALSE;
  else if (status == 0)
    roster_hidden = TRUE;
  else
    roster_hidden = !roster_hidden;

  if (roster_hidden != old_roster_status) {
    if (roster_hidden) {
      // Enter chat mode
      scr_set_chatmode(TRUE);
      scr_ShowBuddyWindow();
    }
    // Recalculate windows size and redraw
    scr_Resize();
    redrawwin(stdscr);
  }
}

inline void scr_WriteMessage(const char *bjid, const char *text,
                             time_t timestamp, guint prefix_flags,
                             unsigned mucnicklen)
{
  char *xtext;

  if (!timestamp) timestamp = time(NULL);

  xtext = ut_expand_tabs(text); // Expand tabs and filter out some chars

  scr_WriteInWindow(bjid, xtext, timestamp, prefix_flags, FALSE, mucnicklen);

  if (xtext != (char*)text)
    g_free(xtext);
}

// If prefix is NULL, HBB_PREFIX_IN is supposed.
void scr_WriteIncomingMessage(const char *jidfrom, const char *text,
        time_t timestamp, guint prefix, unsigned mucnicklen)
{
  if (!(prefix &
        ~HBB_PREFIX_NOFLAG & ~HBB_PREFIX_HLIGHT & ~HBB_PREFIX_HLIGHT_OUT &
        ~HBB_PREFIX_PGPCRYPT))
    prefix |= HBB_PREFIX_IN;

  scr_WriteMessage(jidfrom, text, timestamp, prefix, mucnicklen);
}

void scr_WriteOutgoingMessage(const char *jidto, const char *text, guint prefix)
{
  GSList *roster_elt;
  roster_elt = roster_find(jidto, jidsearch,
                           ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM);

  scr_WriteMessage(jidto, text, 0, prefix|HBB_PREFIX_OUT|HBB_PREFIX_HLIGHT_OUT, 0);

  // Show jidto's buffer unless the buddy is not in the buddylist
  if (roster_elt && g_list_position(buddylist, roster_elt->data) != -1)
    scr_ShowWindow(jidto, FALSE);
}

static inline void set_autoaway(bool setaway)
{
  static enum imstatus oldstatus;
  static char *oldmsg;
  Autoaway = setaway;

  if (setaway) {
    const char *msg, *prevmsg;
    oldstatus = jb_getstatus();
    if (oldmsg) {
      g_free(oldmsg);
      oldmsg = NULL;
    }
    prevmsg = jb_getstatusmsg();
    msg = settings_opt_get("message_autoaway");
    if (!msg)
      msg = prevmsg;
    if (prevmsg)
      oldmsg = g_strdup(prevmsg);
    jb_setstatus(away, NULL, msg, FALSE);
  } else {
    // Back
    jb_setstatus(oldstatus, NULL, (oldmsg ? oldmsg : ""), FALSE);
    if (oldmsg) {
      g_free(oldmsg);
      oldmsg = NULL;
    }
  }
}

long int scr_GetAutoAwayTimeout(time_t now)
{
  enum imstatus cur_st;
  unsigned int autoaway_timeout = settings_opt_get_int("autoaway");

  if (Autoaway || !autoaway_timeout)
    return 86400;

  cur_st = jb_getstatus();
  // Auto-away is disabled for the following states
  if ((cur_st != available) && (cur_st != freeforchat))
    return 86400;

  if (now >= LastActivity + (time_t)autoaway_timeout)
    return 0;
  else
    return LastActivity + (time_t)autoaway_timeout - now;
}

//  set_chatstate(state)
// Set the current chat state (0=active, 1=composing, 2=paused)
// If the chat state has changed, call jb_send_chatstate()
static inline void set_chatstate(int state)
{
#if defined JEP0022 || defined JEP0085
  if (chatstates_disabled)
    return;
  if (!chatmode)
    state = 0;
  if (state != chatstate) {
    chatstate = state;
    if (current_buddy &&
        buddy_gettype(BUDDATA(current_buddy)) == ROSTER_TYPE_USER) {
      guint jep_state;
      if (chatstate == 1)
        jep_state = ROSTER_EVENT_COMPOSING;
      else if (chatstate == 2)
        jep_state = ROSTER_EVENT_PAUSED;
      else
        jep_state = ROSTER_EVENT_ACTIVE;
      jb_send_chatstate(BUDDATA(current_buddy), jep_state);
    }
    if (!chatstate)
      chatstate_timestamp = 0;
  }
#endif
}

#if defined JEP0022 || defined JEP0085
inline long int scr_GetChatStatesTimeout(time_t now)
{
  // Check if we're currently composing...
  if (chatstate != 1 || !chatstate_timestamp)
    return 86400;

  // If the timeout is reached, let's change the state right now.
  if (now >= chatstate_timestamp + COMPOSING_TIMEOUT) {
    chatstate_timestamp = now;
    set_chatstate(2);
    return 86400;
  }

 return chatstate_timestamp + COMPOSING_TIMEOUT - now;
}
#endif

// Check if we should enter/leave automatic away status
void scr_CheckAutoAway(int activity)
{
  enum imstatus cur_st;
  unsigned int autoaway_timeout = settings_opt_get_int("autoaway");

  if (Autoaway && activity) set_autoaway(FALSE);
  if (!autoaway_timeout) return;
  if (!LastActivity || activity) time(&LastActivity);

  cur_st = jb_getstatus();
  // Auto-away is disabled for the following states
  if ((cur_st != available) && (cur_st != freeforchat))
    return;

  if (!activity) {
    time_t now;
    time(&now);
    if (!Autoaway && (now > LastActivity + (time_t)autoaway_timeout))
      set_autoaway(TRUE);
  }
}

//  set_current_buddy(newbuddy)
// Set the current_buddy to newbuddy (if not NULL)
// Lock the newbuddy, and unlock the previous current_buddy
static void set_current_buddy(GList *newbuddy)
{
  enum imstatus prev_st = imstatus_size;
  /* prev_st initialized to imstatus_size, which is used as "undef" value.
   * We are sure prev_st will get a different status value after the
   * buddy_getstatus() call.
   */

  if (!current_buddy || !newbuddy)  return;
  if (newbuddy == current_buddy)    return;

  // We're moving to another buddy.  We're thus inactive wrt current_buddy.
  set_chatstate(0);
  // We don't want the chatstate to be changed again right now.
  lock_chatstate = true;

  prev_st = buddy_getstatus(BUDDATA(current_buddy), NULL);
  buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
  if (chatmode)
    alternate_buddy = current_buddy;
  current_buddy = newbuddy;
  // Lock the buddy in the buddylist if we're in chat mode
  if (chatmode)
    buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, TRUE);
  // We should rebuild the buddylist but not everytime
  // Here we check if we were locking a buddy who is actually offline,
  // and hide_offline_buddies is TRUE.  In which case we need to rebuild.
  if (!(buddylist_get_filter() & 1<<prev_st))
    buddylist_build();
  update_roster = TRUE;
}

//  scr_RosterTop()
// Go to the first buddy in the buddylist
void scr_RosterTop(void)
{
  set_current_buddy(buddylist);
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterBottom()
// Go to the last buddy in the buddylist
void scr_RosterBottom(void)
{
  set_current_buddy(g_list_last(buddylist));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterUp()
// Go to the previous buddy in the buddylist
void scr_RosterUp(void)
{
  set_current_buddy(g_list_previous(current_buddy));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterDown()
// Go to the next buddy in the buddylist
void scr_RosterDown(void)
{
  set_current_buddy(g_list_next(current_buddy));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterPrevGroup()
// Go to the previous group in the buddylist
void scr_RosterPrevGroup(void)
{
  GList *bud;

  for (bud = current_buddy ; bud ; ) {
    bud = g_list_previous(bud);
    if (!bud)
      break;
    if (buddy_gettype(BUDDATA(bud)) & ROSTER_TYPE_GROUP) {
      set_current_buddy(bud);
      if (chatmode)
        scr_ShowBuddyWindow();
      break;
    }
  }
}

//  scr_RosterNextGroup()
// Go to the next group in the buddylist
void scr_RosterNextGroup(void)
{
  GList *bud;

  for (bud = current_buddy ; bud ; ) {
    bud = g_list_next(bud);
    if (!bud)
      break;
    if (buddy_gettype(BUDDATA(bud)) & ROSTER_TYPE_GROUP) {
      set_current_buddy(bud);
      if (chatmode)
        scr_ShowBuddyWindow();
      break;
    }
  }
}

//  scr_RosterSearch(str)
// Look forward for a buddy with jid/name containing str.
void scr_RosterSearch(char *str)
{
  set_current_buddy(buddy_search(str));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterJumpJid(bjid)
// Jump to buddy bjid.
// NOTE: With this function, the buddy is added to the roster if doesn't exist.
void scr_RosterJumpJid(char *barejid)
{
  GSList *roster_elt;
  // Look for an existing buddy
  roster_elt = roster_find(barejid, jidsearch,
                 ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM);
  // Create it if necessary
  if (!roster_elt)
    roster_elt = roster_add_user(barejid, NULL, NULL, ROSTER_TYPE_USER,
                                 sub_none);
  // Set a lock to see it in the buddylist
  buddy_setflags(BUDDATA(roster_elt), ROSTER_FLAG_LOCK, TRUE);
  buddylist_build();
  // Jump to the buddy
  set_current_buddy(buddy_search_jid(barejid));
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterUnreadMessage(next)
// Go to a new message.  If next is not null, try to go to the next new
// message.  If it is not possible or if next is NULL, go to the first new
// message from unread_list.
void scr_RosterUnreadMessage(int next)
{
  gpointer unread_ptr;
  gpointer refbuddata;
  GList *nbuddy;

  if (!current_buddy) return;

  if (next) refbuddata = BUDDATA(current_buddy);
  else      refbuddata = NULL;

  unread_ptr = unread_msg(refbuddata);
  if (!unread_ptr) return;

  if (!(buddy_gettype(unread_ptr) & ROSTER_TYPE_SPECIAL)) {
    gpointer ngroup;
    // If buddy is in a folded group, we need to expand it
    ngroup = buddy_getgroup(unread_ptr);
    if (buddy_getflags(ngroup) & ROSTER_FLAG_HIDE) {
      buddy_setflags(ngroup, ROSTER_FLAG_HIDE, FALSE);
      buddylist_build();
    }
  }

  nbuddy = g_list_find(buddylist, unread_ptr);
  if (nbuddy) {
    set_current_buddy(nbuddy);
    if (chatmode) scr_ShowBuddyWindow();
  } else
    scr_LogPrint(LPRINT_LOGNORM, "Error: nbuddy == NULL"); // should not happen
}

//  scr_RosterJumpAlternate()
// Try to jump to alternate (== previous) buddy
void scr_RosterJumpAlternate(void)
{
  if (!alternate_buddy || g_list_position(buddylist, alternate_buddy) == -1)
    return;
  set_current_buddy(alternate_buddy);
  if (chatmode)
    scr_ShowBuddyWindow();
}

//  scr_RosterDisplay(filter)
// Set the roster filter mask.  If filter is null/empty, the current
// mask is displayed.
void scr_RosterDisplay(const char *filter)
{
  guchar status;
  enum imstatus budstate;
  char strfilter[imstatus_size+1];
  char *psfilter;

  if (filter && *filter) {
    int show_all = (*filter == '*');
    status = 0;
    for (budstate = 0; budstate < imstatus_size-1; budstate++)
      if (strchr(filter, imstatus2char[budstate]) || show_all)
        status |= 1<<budstate;
    buddylist_set_filter(status);
    buddylist_build();
    update_roster = TRUE;
    return;
  }

  // Display current filter
  psfilter = strfilter;
  status = buddylist_get_filter();
  for (budstate = 0; budstate < imstatus_size-1; budstate++)
    if (status & 1<<budstate)
      *psfilter++ = imstatus2char[budstate];
  *psfilter = '\0';
  scr_LogPrint(LPRINT_NORMAL, "Roster status filter: %s", strfilter);
}

//  scr_BufferScrollUpDown()
// Scroll up/down the current buddy window,
// - half a screen if nblines is 0,
// - up if updown == -1, down if updown == 1
void scr_BufferScrollUpDown(int updown, unsigned int nblines)
{
  winbuf *win_entry;
  int n, nbl;
  GList *hbuf_top;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;

  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry  = scr_SearchWindow(CURRENT_JID, isspe);
  if (!win_entry) return;

  if (!nblines) {
    // Scroll half a screen (or less)
    nbl = CHAT_WIN_HEIGHT/2;
  } else {
    nbl = nblines;
  }
  hbuf_top = win_entry->bd->top;

  if (updown == -1) {   // UP
    if (!hbuf_top) {
      hbuf_top = g_list_last(win_entry->bd->hbuf);
      if (!win_entry->bd->cleared) {
        if (!nblines) nbl = nbl*3 - 1;
        else nbl += CHAT_WIN_HEIGHT - 1;
      } else {
        win_entry->bd->cleared = FALSE;
      }
    }
    for (n=0 ; hbuf_top && n < nbl && g_list_previous(hbuf_top) ; n++)
      hbuf_top = g_list_previous(hbuf_top);
    win_entry->bd->top = hbuf_top;
  } else {              // DOWN
    for (n=0 ; hbuf_top && n < nbl ; n++)
      hbuf_top = g_list_next(hbuf_top);
    win_entry->bd->top = hbuf_top;
    // Check if we are at the bottom
    for (n=0 ; hbuf_top && n < CHAT_WIN_HEIGHT-1 ; n++)
      hbuf_top = g_list_next(hbuf_top);
    if (!hbuf_top)
      win_entry->bd->top = NULL; // End reached
  }

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
}

//  scr_BufferClear()
// Clear the current buddy window (used for the /clear command)
void scr_BufferClear(void)
{
  winbuf *win_entry;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = scr_SearchWindow(CURRENT_JID, isspe);
  if (!win_entry) return;

  win_entry->bd->cleared = TRUE;
  win_entry->bd->top = NULL;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
}

//  buffer_purge()
// key: winId/jid
// value: winbuf structure
// data: int, set to 1 if the buffer should be closed.
// NOTE: does not work for special buffers.
static void buffer_purge(gpointer key, gpointer value, gpointer data)
{
  int *p_closebuf = data;
  winbuf *win_entry = value;

  // Delete the current hbuf
  hbuf_free(&win_entry->bd->hbuf);

  if (*p_closebuf) {
    g_hash_table_remove(winbufhash, key);
  } else {
    win_entry->bd->cleared = FALSE;
    win_entry->bd->top = NULL;
  }
}

//  scr_BufferPurge(closebuf, jid)
// Purge/Drop the current buddy buffer or jid's buffer if jid != NULL.
// If closebuf is 1, close the buffer.
void scr_BufferPurge(int closebuf, const char *jid)
{
  winbuf *win_entry;
  guint isspe;
  guint *p_closebuf;
  const char *cjid;

  if (jid) {
    cjid = jid;
    isspe = FALSE;
  } else {
    // Get win_entry
    if (!current_buddy) return;
    cjid = CURRENT_JID;
    isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  }
  win_entry = scr_SearchWindow(cjid, isspe);
  if (!win_entry) return;

  if (!isspe) {
    p_closebuf = g_new(guint, 1);
    *p_closebuf = closebuf;
    buffer_purge((gpointer)cjid, win_entry, p_closebuf);
    g_free(p_closebuf);
    if (closebuf) {
      scr_set_chatmode(FALSE);
      currentWindow = NULL;
    }
  } else {
    // (Special buffer)
    // Reset the current hbuf
    hbuf_free(&win_entry->bd->hbuf);
    // Currently it can only be the status buffer
    statushbuf = NULL;

    win_entry->bd->cleared = FALSE;
    win_entry->bd->top = NULL;
  }

  // Refresh the window
  scr_UpdateBuddyWindow();

  // Finished :)
  update_panels();
}

void scr_BufferPurgeAll(int closebuf)
{
  guint *p_closebuf;
  p_closebuf = g_new(guint, 1);

  *p_closebuf = closebuf;
  g_hash_table_foreach(winbufhash, buffer_purge, p_closebuf);
  g_free(p_closebuf);

  if (closebuf) {
    scr_set_chatmode(FALSE);
    currentWindow = NULL;
  }

  // Refresh the window
  scr_UpdateBuddyWindow();

  // Finished :)
  update_panels();
}

//  scr_BufferScrollLock(lock)
// Lock/unlock the current buddy buffer
// lock = 1 : lock
// lock = 0 : unlock
// lock = -1: toggle lock status
void scr_BufferScrollLock(int lock)
{
  winbuf *win_entry;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = scr_SearchWindow(CURRENT_JID, isspe);
  if (!win_entry) return;

  if (lock == -1)
    lock = !win_entry->bd->lock;

  if (lock) {
    win_entry->bd->lock = TRUE;
  } else {
    win_entry->bd->lock = FALSE;
    //win_entry->bd->cleared = FALSE;
    if (isspe || (buddy_getflags(BUDDATA(current_buddy)) & ROSTER_FLAG_MSG))
      win_entry->bd->top = NULL;
  }

  // If chatmode is disabled and we're at the bottom of the buffer,
  // we need to set the "top" line, so we need to call scr_ShowBuddyWindow()
  // at least once.  (Maybe it will cause a double refresh...)
  if (!chatmode && !win_entry->bd->top) {
    chatmode = TRUE;
    scr_ShowBuddyWindow();
    chatmode = FALSE;
  }

  // Refresh the window
  scr_UpdateBuddyWindow();

  // Finished :)
  update_panels();
}

//  scr_BufferTopBottom()
// Jump to the head/tail of the current buddy window
// (top if topbottom == -1, bottom topbottom == 1)
void scr_BufferTopBottom(int topbottom)
{
  winbuf *win_entry;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = scr_SearchWindow(CURRENT_JID, isspe);
  if (!win_entry) return;

  win_entry->bd->cleared = FALSE;
  if (topbottom == 1)
    win_entry->bd->top = NULL;
  else
    win_entry->bd->top = g_list_first(win_entry->bd->hbuf);

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
}

//  scr_BufferSearch(direction, text)
// Jump to the next line containing text
// (backward search if direction == -1, forward if topbottom == 1)
void scr_BufferSearch(int direction, const char *text)
{
  winbuf *win_entry;
  GList *current_line, *search_res;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = scr_SearchWindow(CURRENT_JID, isspe);
  if (!win_entry) return;

  if (win_entry->bd->top)
    current_line = win_entry->bd->top;
  else
    current_line = g_list_last(win_entry->bd->hbuf);

  search_res = hbuf_search(current_line, direction, text);

  if (search_res) {
    win_entry->bd->cleared = FALSE;
    win_entry->bd->top = search_res;

    // Refresh the window
    scr_UpdateWindow(win_entry);

    // Finished :)
    update_panels();
  } else
    scr_LogPrint(LPRINT_NORMAL, "Search string not found");
}

//  scr_BufferPercent(n)
// Jump to the specified position in the buffer, in %
void scr_BufferPercent(int pc)
{
  winbuf *win_entry;
  GList *search_res;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = scr_SearchWindow(CURRENT_JID, isspe);
  if (!win_entry) return;

  if (pc < 0 || pc > 100) {
    scr_LogPrint(LPRINT_NORMAL, "Bad % value");
    return;
  }

  search_res = hbuf_jump_percent(win_entry->bd->hbuf, pc);

  win_entry->bd->cleared = FALSE;
  win_entry->bd->top = search_res;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
}

//  scr_BufferDate(t)
// Jump to the first line after date t in the buffer
// t is a date in seconds since `00:00:00 1970-01-01 UTC'
void scr_BufferDate(time_t t)
{
  winbuf *win_entry;
  GList *search_res;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = scr_SearchWindow(CURRENT_JID, isspe);
  if (!win_entry) return;

  search_res = hbuf_jump_date(win_entry->bd->hbuf, t);

  win_entry->bd->cleared = FALSE;
  win_entry->bd->top = search_res;

  // Refresh the window
  scr_UpdateWindow(win_entry);

  // Finished :)
  update_panels();
}

#ifdef DEBUG_ENABLE
//  buffer_list()
// key: winId/jid
// value: winbuf structure
// data: none.
static void buffer_list(gpointer key, gpointer value, gpointer data)
{
  GList *head;
  winbuf *win_entry = value;

  head = g_list_first(win_entry->bd->hbuf);

  scr_LogPrint(LPRINT_NORMAL, " %s  (%u/%u)", key,
               g_list_length(head), hbuf_get_blocks_number(head));
}

void scr_BufferList(void)
{
  scr_LogPrint(LPRINT_NORMAL, "Buffer list:");
  buffer_list("[status]", statusWindow, NULL);
  g_hash_table_foreach(winbufhash, buffer_list, NULL);
  scr_LogPrint(LPRINT_NORMAL, "End of buffer list.");
  scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
  update_roster = TRUE;
}
#endif

//  scr_set_chatmode()
// Public function to (un)set chatmode...
inline void scr_set_chatmode(int enable)
{
  chatmode = enable;
  scr_UpdateChatStatus(TRUE);
}

//  scr_get_chatmode()
// Public function to get chatmode state.
inline int scr_get_chatmode(void)
{
  return chatmode;
}

//  scr_get_multimode()
// Public function to get multimode status...
inline int scr_get_multimode(void)
{
  return multimode;
}

//  scr_setmsgflag_if_needed(jid)
// Set the message flag unless we're already in the jid buffer window
void scr_setmsgflag_if_needed(const char *bjid, int special)
{
  const char *current_id;
  bool iscurrentlocked = FALSE;

  if (!bjid)
    return;

  if (current_buddy) {
    if (special)
      current_id = buddy_getname(BUDDATA(current_buddy));
    else
      current_id = buddy_getjid(BUDDATA(current_buddy));
    if (current_id) {
      winbuf *win_entry = scr_SearchWindow(current_id, special);
      if (!win_entry) return;
      iscurrentlocked = win_entry->bd->lock;
    }
  } else {
    current_id = NULL;
  }
  if (!chatmode || !current_id || strcmp(bjid, current_id) || iscurrentlocked)
    roster_msg_setflag(bjid, special, TRUE);
}

//  scr_set_multimode()
// Public function to (un)set multimode...
// Convention:
//  0 = disabled / 1 = multimode / 2 = multimode verbatim (commands disabled)
inline void scr_set_multimode(int enable, char *subject)
{
  g_free(multiline);
  multiline = NULL;

  g_free(multimode_subj);
  if (enable && subject)
    multimode_subj = g_strdup(subject);
  else
    multimode_subj = NULL;

  multimode = enable;
}

//  scr_get_multiline()
// Public function to get the current multi-line.
inline const char *scr_get_multiline(void)
{
  if (multimode && multiline)
    return multiline;
  return NULL;
}

//  scr_get_multimode_subj()
// Public function to get the multi-line subject, if any.
inline const char *scr_get_multimode_subj(void)
{
  if (multimode)
    return multimode_subj;
  return NULL;
}

//  scr_append_multiline(line)
// Public function to append a line to the current multi-line message.
// Skip empty leading lines.
void scr_append_multiline(const char *line)
{
  static int num;

  if (!multimode) {
    scr_LogPrint(LPRINT_NORMAL, "Error: Not in multi-line message mode!");
    return;
  }
  if (multiline) {
    int len = strlen(multiline)+strlen(line)+2;
    if (len >= HBB_BLOCKSIZE - 1) {
      // We don't handle single messages with size > HBB_BLOCKSIZE
      // (see hbuf)
      scr_LogPrint(LPRINT_NORMAL, "Your multi-line message is too big, "
                   "this line has not been added.");
      scr_LogPrint(LPRINT_NORMAL, "Please send this part now...");
      return;
    }
    if (num >= MULTILINE_MAX_LINE_NUMBER) {
      // We don't allow too many lines; however the maximum is arbitrary
      // (It should be < 1000 yet)
      scr_LogPrint(LPRINT_NORMAL, "Your message has too many lines, "
                   "this one has not been added.");
      scr_LogPrint(LPRINT_NORMAL, "Please send this part now...");
      return;
    }
    multiline = g_renew(char, multiline, len);
    strcat(multiline, "\n");
    strcat(multiline, line);
    num++;
  } else {
    // First message line (we skip leading empty lines)
    num = 0;
    if (line[0]) {
      multiline = g_strdup(line);
      num++;
    } else
      return;
  }
  scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
               "Multi-line mode: line #%d added  [%.25s...", num, line);
}

//  scr_cmdhisto_addline()
// Add a line to the inputLine history
inline void scr_cmdhisto_addline(char *line)
{
  int max_histo_lines;

  if (!line || !*line)
    return;

  max_histo_lines = settings_opt_get_int("cmdhistory_lines");

  if (max_histo_lines < 0)
    max_histo_lines = 1;

  if (max_histo_lines)
    while (cmdhisto_nblines >= (guint)max_histo_lines) {
      if (cmdhisto_cur && cmdhisto_cur == cmdhisto)
        break;
      g_free(cmdhisto->data);
      cmdhisto = g_list_delete_link(cmdhisto, cmdhisto);
      cmdhisto_nblines--;
    }

  cmdhisto = g_list_append(cmdhisto, g_strdup(line));
  cmdhisto_nblines++;
}

//  scr_cmdhisto_prev()
// Look for previous line beginning w/ the given mask in the inputLine history
// Returns NULL if none found
static const char *scr_cmdhisto_prev(char *mask, guint len)
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
static const char *scr_cmdhisto_next(char *mask, guint len)
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

//  readline_transpose_chars()
// Drag  the  character  before point forward over the character at
// point, moving point forward as well.  If point is at the end  of
// the  line, then this transposes the two characters before point.
void readline_transpose_chars(void)
{
  char *c1, *c2;
  unsigned a, b;

  if (ptr_inputline == inputLine) return;

  if (!*ptr_inputline) { // We're at EOL
    // If line is only 1 char long, nothing to do...
    if (ptr_inputline == prev_char(ptr_inputline, inputLine)) return;
    // Transpose the two previous characters
    c2 = prev_char(ptr_inputline, inputLine);
    c1 = prev_char(c2, inputLine);
    a = get_char(c1);
    b = get_char(c2);
    put_char(put_char(c1, b), a);
  } else {
    // Swap the two characters before the cursor and move right.
    c2 = ptr_inputline;
    c1 = prev_char(c2, inputLine);
    a = get_char(c1);
    b = get_char(c2);
    put_char(put_char(c1, b), a);
    check_offset(1);
  }
}

void readline_forward_kill_word(void)
{
  char *c, *old = ptr_inputline;
  int spaceallowed = 1;

  if (! *ptr_inputline) return;

  for (c = ptr_inputline ; *c ; c = next_char(c)) {
    if (!iswalnum(get_char(c))) {
      if (iswblank(get_char(c))) {
        if (!spaceallowed) break;
      } else spaceallowed = 0;
    } else spaceallowed = 0;
  }

  // Modify the line
  for (;;) {
    *old = *c++;
    if (!*old++) break;
  }
}

//  readline_backward_kill_word()
// Kill the word before the cursor, in input line
void readline_backward_kill_word(void)
{
  char *c, *old = ptr_inputline;
  int spaceallowed = 1;

  if (ptr_inputline == inputLine) return;

  c = prev_char(ptr_inputline, inputLine);
  for ( ; c > inputLine ; c = prev_char(c, inputLine)) {
    if (!iswalnum(get_char(c))) {
      if (iswblank(get_char(c))) {
        if (!spaceallowed) break;
      } else spaceallowed = 0;
    } else spaceallowed = 0;
  }

  if (c == inputLine && *c == COMMAND_CHAR && old != c+1) {
      c = next_char(c);
  } else if (c != inputLine || iswblank(get_char(c))) {
    if ((c < prev_char(ptr_inputline, inputLine)) && (!iswalnum(get_char(c))))
      c = next_char(c);
  }

  // Modify the line
  ptr_inputline = c;
  for (;;) {
    *c = *old++;
    if (!*c++) break;
  }
  check_offset(-1);
}

//  readline_backward_word()
// Move  back  to the start of the current or previous word
void readline_backward_word(void)
{
  int i = 0;

  if (ptr_inputline == inputLine) return;

  if (iswalnum(get_char(ptr_inputline)) &&
      !iswalnum(get_char(prev_char(ptr_inputline, inputLine))))
    i--;

  for( ;
      ptr_inputline > inputLine;
      ptr_inputline = prev_char(ptr_inputline, inputLine)) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (i) {
        ptr_inputline = next_char(ptr_inputline);
        break;
      }
    } else i++;
  }

  check_offset(-1);
}

//  readline_forward_word()
// Move forward to the end of the next word
void readline_forward_word(void)
{
  int stopsymbol_allowed = 1;

  while (*ptr_inputline) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (!stopsymbol_allowed) break;
    } else stopsymbol_allowed = 0;
    ptr_inputline = next_char(ptr_inputline);
  }

  check_offset(1);
}

void readline_updowncase_word(int upcase)
{
  int stopsymbol_allowed = 1;

  while (*ptr_inputline) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (!stopsymbol_allowed) break;
    } else {
      stopsymbol_allowed = 0;
      if (upcase)
        *ptr_inputline = towupper(get_char(ptr_inputline));
      else
        *ptr_inputline = towlower(get_char(ptr_inputline));
    }
    ptr_inputline = next_char(ptr_inputline);
  }

  check_offset(1);
}

void readline_capitalize_word(void)
{
  int stopsymbol_allowed = 1;
  int upcased = 0;

  while (*ptr_inputline) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (!stopsymbol_allowed) break;
    } else {
      stopsymbol_allowed = 0;
      if (!upcased) {
        *ptr_inputline = towupper(get_char(ptr_inputline));
        upcased = 1;
      } else *ptr_inputline = towlower(get_char(ptr_inputline));
    }
    ptr_inputline = next_char(ptr_inputline);
  }

  check_offset(1);
}

void readline_backward_char(void)
{
  if (ptr_inputline == (char*)&inputLine) return;

  ptr_inputline = prev_char(ptr_inputline, inputLine);
  check_offset(-1);
}

void readline_forward_char(void)
{
  if (!*ptr_inputline) return;

  ptr_inputline = next_char(ptr_inputline);
  check_offset(1);
}

//  readline_accept_line(down_history)
// Validate current command line.
// If down_history is true, load the next history line.
int readline_accept_line(int down_history)
{
  scr_CheckAutoAway(TRUE);
  if (process_line(inputLine))
    return 255;
  // Add line to history
  scr_cmdhisto_addline(inputLine);
  // Reset the line
  ptr_inputline = inputLine;
  *ptr_inputline = 0;
  inputline_offset = 0;

  if (down_history) {
    // Use next history line instead of a blank line
    const char *l = scr_cmdhisto_next("", 0);
    if (l) strcpy(inputLine, l);
    // Reset backup history line
    cmdhisto_backup[0] = 0;
  } else {
    // Reset history line pointer
    cmdhisto_cur = NULL;
  }
  return 0;
}

void readline_cancel_completion(void)
{
  scr_cancel_current_completion();
  scr_end_current_completion();
  check_offset(-1);
}

void readline_do_completion(void)
{
  int i, n;

  if (scr_get_multimode() != 2) {
    // Not in verbatim multi-line mode
    scr_handle_tab();
  } else {
    // Verbatim multi-line mode: expand tab
    char tabstr[9];
    n = 8 - (ptr_inputline - inputLine) % 8;
    for (i = 0; i < n; i++)
      tabstr[i] = ' ';
    tabstr[i] = '\0';
    scr_insert_text(tabstr);
  }
  check_offset(0);
}

void readline_refresh_screen(void)
{
  scr_CheckAutoAway(TRUE);
  ParseColors();
  scr_Resize();
  redrawwin(stdscr);
}

void readline_disable_chat_mode(void)
{
  scr_CheckAutoAway(TRUE);
  currentWindow = NULL;
  chatmode = FALSE;
  if (current_buddy)
    buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
  scr_RosterVisibility(1);
  scr_UpdateChatStatus(FALSE);
  top_panel(chatPanel);
  top_panel(inputPanel);
  update_panels();
}

void readline_hist_beginning_search_bwd(void)
{
  const char *l = scr_cmdhisto_prev(inputLine, ptr_inputline-inputLine);
  if (l) strcpy(inputLine, l);
}

void readline_hist_beginning_search_fwd(void)
{
  const char *l = scr_cmdhisto_next(inputLine, ptr_inputline-inputLine);
  if (l) strcpy(inputLine, l);
}

void readline_hist_prev(void)
{
  const char *l = scr_cmdhisto_prev(inputLine, 0);
  if (l) {
    strcpy(inputLine, l);
    // Set the pointer at the EOL.
    // We have to move it to BOL first, because we could be too far already.
    readline_iline_start();
    readline_iline_end();
  }
}

void readline_hist_next(void)
{
  const char *l = scr_cmdhisto_next(inputLine, 0);
  if (l) {
    strcpy(inputLine, l);
    // Set the pointer at the EOL.
    // We have to move it to BOL first, because we could be too far already.
    readline_iline_start();
    readline_iline_end();
  }
}

void readline_backward_kill_char(void)
{
  char *src, *c;

  if (ptr_inputline == (char*)&inputLine)
    return;

  src = ptr_inputline;
  c = prev_char(ptr_inputline, inputLine);
  ptr_inputline = c;
  for ( ; *src ; )
    *c++ = *src++;
  *c = 0;
  check_offset(-1);
}

void readline_forward_kill_char(void)
{
  if (!*ptr_inputline)
    return;

  strcpy(ptr_inputline, next_char(ptr_inputline));
}

void readline_iline_start(void)
{
  ptr_inputline = inputLine;
  inputline_offset = 0;
}

void readline_iline_end(void)
{
  for (; *ptr_inputline; ptr_inputline++) ;
  check_offset(1);
}

void readline_backward_kill_iline(void)
{
  strcpy(inputLine, ptr_inputline);
  ptr_inputline = inputLine;
  inputline_offset = 0;
}

void readline_forward_kill_iline(void)
{
  *ptr_inputline = 0;
}

void readline_send_multiline(void)
{
  // Validate current multi-line
  if (scr_get_multimode())
    process_command(mkcmdstr("msay send"), TRUE);
}

//  which_row()
// Tells which row our cursor is in, in the command line.
// -2 -> normal text
// -1 -> room: nickname completion
//  0 -> command
//  1 -> parameter 1 (etc.)
//  If > 0, then *p_row is set to the beginning of the row
static int which_row(const char **p_row)
{
  int row = -1;
  char *p;
  int quote = FALSE;

  // Not a command?
  if ((ptr_inputline == inputLine) || (inputLine[0] != COMMAND_CHAR)) {
    if (!current_buddy) return -2;
    if (buddy_gettype(BUDDATA(current_buddy)) == ROSTER_TYPE_ROOM) {
      *p_row = inputLine;
      return -1;
    }
    return -2;
  }

  // This is a command
  row = 0;
  for (p = inputLine ; p < ptr_inputline ; p = next_char(p)) {
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
static void scr_insert_text(const char *text)
{
  char tmpLine[INPUTLINE_LENGTH+1];
  int len = strlen(text);
  // Check the line isn't too long
  if (strlen(inputLine) + len >= INPUTLINE_LENGTH) {
    scr_LogPrint(LPRINT_LOGNORM, "Cannot insert text, line too long.");
    return;
  }

  strcpy(tmpLine, ptr_inputline);
  strcpy(ptr_inputline, text);
  ptr_inputline += len;
  strcpy(ptr_inputline, tmpLine);
}

static void scr_cancel_current_completion(void);

//  scr_handle_tab()
// Function called when tab is pressed.
// Initiate or continue a completion...
static void scr_handle_tab(void)
{
  int nrow;
  const char *row;
  const char *cchar;
  guint compl_categ;

  row = inputLine; // (Kills a GCC warning)
  nrow = which_row(&row);

  // a) No completion if no leading slash ('cause not a command),
  //    unless this is a room (then, it is a nickname completion)
  // b) We can't have more than 2 parameters (we use 2 flags)
  if ((nrow == -2) || (nrow == 3 && !completion_started) || nrow > 3)
    return;

  if (nrow == 0) {          // Command completion
    row = next_char(inputLine);
    compl_categ = COMPL_CMD;
  } else if (nrow == -1) {  // Nickname completion
    compl_categ = COMPL_RESOURCE;
  } else {                  // Other completion, depending on the command
    int alias = FALSE;
    cmd *com;
    char *xpline = expandalias(inputLine);
    com = cmd_get(xpline);
    if (xpline != inputLine) {
      // This is an alias, so we can't complete rows > 0
      alias = TRUE;
      g_free(xpline);
    }
    if ((!com && (!alias || !completion_started)) || !row) {
      scr_LogPrint(LPRINT_NORMAL, "I cannot complete that...");
      return;
    }
    if (!alias)
      compl_categ = com->completion_flags[nrow-1];
    else
      compl_categ = 0;
  }

  if (!completion_started) {
    guint dynlist;
    GSList *list = compl_get_category_list(compl_categ, &dynlist);
    if (list) {
      guint n;
      char *prefix = g_strndup(row, ptr_inputline-row);
      // Init completion
      n = new_completion(prefix, list);
      g_free(prefix);
      if (n == 0 && nrow == -1) {
        // This is a MUC room and we can't complete from the beginning of the
        // line.  Let's try a bit harder and complete the current word.
        row = prev_char(ptr_inputline, inputLine);
        while (row >= inputLine) {
          if (iswspace(get_char(row)) || get_char(row) == '(') {
              row = next_char((char*)row);
              break;
          }
          if (row == inputLine)
            break;
          row = prev_char((char*)row, inputLine);
        }
        // There's no need to try again if row == inputLine
        if (row > inputLine) {
          prefix = g_strndup(row, ptr_inputline-row);
          new_completion(prefix, list);
          g_free(prefix);
        }
      }
      // Free the list if it's a dynamic one
      if (dynlist) {
        GSList *slp;
        for (slp = list; slp; slp = g_slist_next(slp))
          g_free(slp->data);
        g_slist_free(list);
      }
      // Now complete
      cchar = complete();
      if (cchar)
        scr_insert_text(cchar);
      completion_started = TRUE;
    }
  } else {      // Completion already initialized
    scr_cancel_current_completion();
    // Now complete again
    cchar = complete();
    if (cchar)
      scr_insert_text(cchar);
  }
}

static void scr_cancel_current_completion(void)
{
  char *c;
  char *src = ptr_inputline;
  guint back = cancel_completion();
  guint i;
  // Remove $back chars
  for (i = 0; i < back; i++)
    ptr_inputline = prev_char(ptr_inputline, inputLine);
  c = ptr_inputline;
  for ( ; *src ; )
    *c++ = *src++;
  *c = 0;
}

static void scr_end_current_completion(void)
{
  done_completion();
  completion_started = FALSE;
}

//  check_offset(int direction)
// Check inputline_offset value, and make sure the cursor is inside the
// screen.
static inline void check_offset(int direction)
{
  int i;
  char *c = &inputLine[inputline_offset];
  // Left side
  if (inputline_offset && direction <= 0) {
    while (ptr_inputline <= c) {
      for (i = 0; i < 5; i++)
        c = prev_char(c, inputLine);
      if (c == inputLine)
        break;
    }
  }
  // Right side
  if (direction >= 0) {
    int delta = get_char_width(c);
    while (ptr_inputline > c) {
      c = next_char(c);
      delta += get_char_width(c);
    }
    c = &inputLine[inputline_offset];
    while (delta >= maxX) {
      for (i = 0; i < 5; i++) {
        delta -= get_char_width(c);
        c = next_char(c);
      }
    }
  }
  inputline_offset = c - inputLine;
}

#ifdef HAVE_ASPELL_H
// prints inputLine with underlined words when misspelled
static inline void print_checked_line(void)
{
  char *wprint_char_fmt = "%c";
  int point;
  char *ptrCur = inputLine + inputline_offset;

#ifdef UNICODE
  // We need this to display a single UTF-8 char... Any better solution?
  if (utf8_mode)
    wprint_char_fmt = "%lc";
#endif

  wmove(inputWnd, 0, 0); // problem with backspace

  while (*ptrCur) {
    point = ptrCur - inputLine;
    if (maskLine[point])
      wattrset(inputWnd, A_UNDERLINE);
    wprintw(inputWnd, wprint_char_fmt, get_char(ptrCur));
    wattrset(inputWnd, A_NORMAL);
    ptrCur = next_char(ptrCur);
  }
}
#endif

static inline void refresh_inputline(void)
{
#ifdef HAVE_ASPELL_H
  if (settings_opt_get_int("aspell_enable")) {
    memset(maskLine, 0, INPUTLINE_LENGTH+1);
    spellcheck(inputLine, maskLine);
  }
  print_checked_line();
  wclrtoeol(inputWnd);
  if (*ptr_inputline) {
    // hack to set cursor pos. Characters can have different width,
    // so I know of no better way.
    char c = *ptr_inputline;
    *ptr_inputline = 0;
    print_checked_line();
    *ptr_inputline = c;
  }
#else
  mvwprintw(inputWnd, 0, 0, "%s", inputLine + inputline_offset);
  wclrtoeol(inputWnd);
  if (*ptr_inputline) {
    // hack to set cursor pos. Characters can have different width,
    // so I know of no better way.
    char c = *ptr_inputline;
    *ptr_inputline = 0;
    mvwprintw(inputWnd, 0, 0, "%s", inputLine + inputline_offset);
    *ptr_inputline = c;
  }
#endif
}

void scr_handle_CtrlC(void)
{
  if (!Curses) return;
  // Leave multi-line mode
  process_command(mkcmdstr("msay abort"), TRUE);
  // Same as Ctrl-g, now
  scr_cancel_current_completion();
  scr_end_current_completion();
  check_offset(-1);
  refresh_inputline();
}

static void add_keyseq(char *seqstr, guint mkeycode, gint value)
{
  keyseq *ks;

  // Let's make sure the length is correct
  if (strlen(seqstr) > MAX_KEYSEQ_LENGTH) {
    scr_LogPrint(LPRINT_LOGNORM, "add_keyseq(): key sequence is too long!");
    return;
  }

  ks = g_new0(keyseq, 1);
  ks->seqstr = g_strdup(seqstr);
  ks->mkeycode = mkeycode;
  ks->value = value;
  keyseqlist = g_slist_append(keyseqlist, ks);
}

//  match_keyseq(iseq, &ret)
// Check if "iseq" is a known key escape sequence.
// Return value:
// -1  if "seq" matches no known sequence
//  0  if "seq" could match 1 or more known sequences
// >0  if "seq" matches a key sequence; the mkey code is returned
//     and *ret is set to the matching keyseq structure.
static inline gint match_keyseq(int *iseq, keyseq **ret)
{
  GSList *ksl;
  keyseq *ksp;
  char *p, c;
  int *i;
  int needmore = FALSE;

  for (ksl = keyseqlist; ksl; ksl = g_slist_next(ksl)) {
    ksp = ksl->data;
    p = ksp->seqstr;
    i = iseq;
    while (1) {
      c = (unsigned char)*i;
      if (!*p && !c) { // Match
        (*ret) = ksp;
        return ksp->mkeycode;
      }
      if (!c) {
        // iseq is too short
        needmore = TRUE;
        break;
      } else if (!*p || c != *p) {
        // This isn't a match
        break;
      }
      p++; i++;
    }
  }

  if (needmore)
    return 0;
  return -1;
}

static inline int match_utf8_keyseq(int *iseq)
{
  int *strp = iseq;
  unsigned c = *strp++;
  unsigned mask = 0x80;
  int len = -1;
  while (c & mask) {
    mask >>= 1;
    len++;
  }
  if (len <= 0 || len > 4)
    return -1;
  c &= mask - 1;
  while ((*strp & 0xc0) == 0x80) {
    if (len-- <= 0) // can't happen
      return -1;
    c = (c << 6) | (*strp++ & 0x3f);
  }
  if (len)
    return 0;
  return c;
}

void scr_Getch(keycode *kcode)
{
  keyseq *mks = NULL;
  int  ks[MAX_KEYSEQ_LENGTH+1];
  int i;

  memset(kcode, 0, sizeof(keycode));
  memset(ks,  0, sizeof(ks));

  kcode->value = wgetch(inputWnd);
  if (utf8_mode) {
    bool ismeta = (kcode->value == 27);

    if (ismeta)
      ks[0] = wgetch(inputWnd);
    else
      ks[0] = kcode->value;

    for (i = 0; i < MAX_KEYSEQ_LENGTH - 1; i++) {
      int match = match_utf8_keyseq(ks);
      if (match == -1)
        break;
      if (match > 0) {
        kcode->value = match;
        kcode->utf8 = 1;
        if (ismeta)
          kcode->mcode = MKEY_META;
        return;
      }
      ks[i + 1] = wgetch(inputWnd);
      if (ks[i + 1] == ERR)
        break;
    }
    while (i > 0)
      ungetch(ks[i--]);
    if (ismeta)
      ungetch(ks[0]);
    memset(ks,  0, sizeof(ks));
  }
  if (kcode->value != 27)
    return;

  // Check for escape key sequence
  for (i=0; i < MAX_KEYSEQ_LENGTH; i++) {
    int match;
    ks[i] = wgetch(inputWnd);
    if (ks[i] == ERR) break;
    match = match_keyseq(ks, &mks);
    if (match == -1) {
      // No such key sequence.  Let's increment i as it is a valid key.
      i++;
      break;
    }
    if (match > 0) {
      // We have a matching sequence
      kcode->mcode = mks->mkeycode;
      kcode->value = mks->value;
      return;
    }
  }

  // No match.  Let's return a meta-key.
  if (i > 0) {
    kcode->mcode = MKEY_META;
    kcode->value = ks[0];
  }
  if (i > 1) {
    // We need to push some keys back to the keyboard buffer
    while (i-- > 1)
      ungetch(ks[i]);
  }
  return;
}

inline void scr_DoUpdate(void)
{
  doupdate();
}

static int bindcommand(keycode kcode)
{
  gchar asciikey[16], asciicode[16];
  const gchar *boundcmd;

  if (kcode.utf8)
    g_snprintf(asciicode, 15, "U%d", kcode.value);
  else
    g_snprintf(asciicode, 15, "%d", kcode.value);

  if (!kcode.mcode || kcode.mcode == MKEY_EQUIV)
    g_snprintf(asciikey, 15, "%s", asciicode);
  else if (kcode.mcode == MKEY_META)
    g_snprintf(asciikey, 15, "M%s", asciicode);
  else
    g_snprintf(asciikey, 15, "MK%d", kcode.mcode);

  boundcmd = settings_get(SETTINGS_TYPE_BINDING, asciikey);

  if (boundcmd) {
    gchar *cmdline, *boundcmd_locale;
    boundcmd_locale = from_utf8(boundcmd);
    cmdline = g_strdup_printf(mkcmdstr("%s"), boundcmd_locale);
    scr_CheckAutoAway(TRUE);
    if (process_command(cmdline, TRUE))
      return 255; // Quit
    g_free(boundcmd_locale);
    g_free(cmdline);
    return 0;
  }

  scr_LogPrint(LPRINT_NORMAL, "Unknown key=%s", asciikey);
#ifndef UNICODE
  if (utf8_mode)
    scr_LogPrint(LPRINT_NORMAL,
                 "WARNING: Compiled without full UTF-8 support!");
#endif
  return -1;
}

//  process_key(key)
// Handle the pressed key, in the command line (bottom).
int process_key(keycode kcode)
{
  int key = kcode.value;
  int display_char = FALSE;

  lock_chatstate = false;

  switch (kcode.mcode) {
    case 0:
        break;
    case MKEY_EQUIV:
        key = kcode.value;
        break;
    case MKEY_META:
    default:
        if (bindcommand(kcode) == 255)
          return 255;
        key = ERR; // Do not process any further
  }

  if (kcode.utf8) {
    if (key != ERR && !kcode.mcode)
      display_char = TRUE;
    goto display;
  }

  switch (key) {
    case 0:
    case ERR:
        break;
    case 9:     // Tab
        readline_do_completion();
        break;
    case 13:    // Enter
        if (readline_accept_line(FALSE) == 255)
          return 255;
        break;
    case 3:     // Ctrl-C
        scr_handle_CtrlC();
        break;
    case KEY_RESIZE:
        scr_Resize();
        break;
    default:
        display_char = TRUE;
  } // switch

display:
  if (display_char) {
    if (kcode.utf8 ? iswprint(key) : (isprint(key) && !is_speckey(key))) {
      char tmpLine[INPUTLINE_LENGTH+1];

      // Check the line isn't too long
      if (strlen(inputLine) + 4 > INPUTLINE_LENGTH)
        return 0;

      // Insert char
      strcpy(tmpLine, ptr_inputline);
      ptr_inputline = put_char(ptr_inputline, key);
      strcpy(ptr_inputline, tmpLine);
      check_offset(1);
    } else {
      // Look for a key binding.
      if (!kcode.utf8 && (bindcommand(kcode) == 255))
        return 255;
    }
  }

  if (completion_started && key != 9 && key != KEY_RESIZE)
    scr_end_current_completion();
  refresh_inputline();

  if (!lock_chatstate) {
    // Set chat state to composing (1) if the user is currently composing,
    // i.e. not an empty line and not a command line.
    if (inputLine[0] == 0 || inputLine[0] == COMMAND_CHAR)
      set_chatstate(0);
    else
      set_chatstate(1);
    if (chatstate)
      time(&chatstate_timestamp);
  }
  return 0;
}

#ifdef HAVE_ASPELL_H
// Aspell initialization
void spellcheck_init(void)
{
  int aspell_enable           = settings_opt_get_int("aspell_enable");
  const char *aspell_lang     = settings_opt_get("aspell_lang");
  const char *aspell_encoding = settings_opt_get("aspell_encoding");
  AspellCanHaveError *possible_err;

  if (!aspell_enable)
    return;

  if (spell_checker) {
    delete_aspell_speller(spell_checker);
    delete_aspell_config(spell_config);
    spell_checker = NULL;
    spell_config = NULL;
  }

  spell_config = new_aspell_config();
  aspell_config_replace(spell_config, "encoding", aspell_encoding);
  aspell_config_replace(spell_config, "lang", aspell_lang);
  possible_err = new_aspell_speller(spell_config);

  if (aspell_error_number(possible_err) != 0) {
    spell_checker = NULL;
    delete_aspell_config(spell_config);
    spell_config = NULL;
  } else {
    spell_checker = to_aspell_speller(possible_err);
  }
}

// Deinitialization of Aspell spellchecker
void spellcheck_deinit(void)
{
  if (spell_checker) {
    delete_aspell_speller(spell_checker);
    spell_checker = NULL;
  }

  if (spell_config) {
    delete_aspell_config(spell_config);
    spell_config = NULL;
  }
}

#define aspell_isalpha(c) (utf8_mode ? iswalpha(get_char(c)) : isalpha(*c))

// Spell checking function
static void spellcheck(char *line, char *checked)
{
  const char *start, *line_start;

  if (inputLine[0] == 0 || inputLine[0] == COMMAND_CHAR)
    return;

  line_start = line;

  while (*line) {

    if (!aspell_isalpha(line)) {
      line = next_char(line);
      continue;
    }

    if (!strncmp(line, "http://", 7)) {
      line += 7; // : and / characters are 1 byte long in utf8, right?

      while (!strchr(" \t\r\n", *line))
        line = next_char(line); // i think line++ would be fine here?

      continue;
    }

    if (!strncmp(line, "ftp://", 6)) {
      line += 6;

      while (!strchr(" \t\r\n", *line))
        line = next_char(line);

      continue;
    }

    start = line;

    while (aspell_isalpha(line))
      line = next_char(line);

    if (spell_checker &&
        aspell_speller_check(spell_checker, start, line - start) == 0)
      memset(&checked[start - line_start], ASPELLBADCHAR, line - start);
  }
}
#endif

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
