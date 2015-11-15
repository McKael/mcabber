/*
 * main.c
 *
 * Copyright (C) 2005-2014 Mikael Berthe <mikael@lilotux.net>
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
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>
#include <config.h>
#include <poll.h>
#include <errno.h>

#include "caps.h"
#include "screen.h"
#include "settings.h"
#include "roster.h"
#include "commands.h"
#include "histolog.h"
#include "hooks.h"
#include "utils.h"
#include "pgp.h"
#include "otr.h"
#include "xmpp.h"
#include "help.h"
#include "events.h"
#include "compl.h"

#ifndef MODULES_ENABLE
# include "fifo.h"
#endif

#ifdef MODULES_ENABLE
# include "modules.h"
#endif

#ifdef ENABLE_HGCSET
# include "hgcset.h"
#endif

#ifndef WAIT_ANY
# define WAIT_ANY -1
#endif

#ifdef USE_SIGWINCH
void sigwinch_resize(void);
static bool sigwinch;
#endif

static bool terminate_ui;
GMainContext *main_context;

static struct termios *backup_termios;

char *mcabber_version(void)
{
  char *ver;
#ifdef HGCSET
  ver = g_strdup_printf("%s (%s)", PACKAGE_VERSION, HGCSET);
#else
  ver = g_strdup(PACKAGE_VERSION);
#endif
  return ver;
}

static void mcabber_terminate(const char *msg)
{
#ifndef MODULES_ENABLE
  fifo_deinit();
#endif
  xmpp_disconnect();
  scr_terminate_curses();

  // Restore term settings, if needed.
  if (backup_termios)
    tcsetattr(fileno(stdin), TCSAFLUSH, backup_termios);

  if (msg)
    fprintf(stderr, "%s\n", msg);
  printf("Bye!\n");
  exit(EXIT_SUCCESS);
}

void sig_handler(int signum)
{
  if (signum == SIGCHLD) {
    int status;
    pid_t pid;
    do {
      pid = waitpid (WAIT_ANY, &status, WNOHANG);
      // Check the exit status value if 'eventcmd_checkstatus' is set
      if (settings_opt_get_int("eventcmd_checkstatus")) {
        if (pid > 0) {
          // exit status 2 -> beep
          if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
            scr_beep();
          }
        }
      }
    } while (pid > 0);
    signal(SIGCHLD, sig_handler);
  } else if (signum == SIGTERM) {
    mcabber_terminate("Killed by SIGTERM");
  } else if (signum == SIGINT) {
    mcabber_terminate("Killed by SIGINT");
  } else if (signum == SIGHUP) {
    mcabber_terminate("Killed by SIGHUP");
#ifdef USE_SIGWINCH
  } else if (signum == SIGWINCH) {
    sigwinch = TRUE;
#endif
  } else {
    scr_LogPrint(LPRINT_LOGNORM, "Caught signal: %d", signum);
  }
}

//  ask_password(what)
// Return the password, or NULL.
// The string must be freed after use.
static char *ask_password(const char *what)
{
  char *password, *p;
  size_t passsize = 128;
  struct termios orig, new;

  password = g_new0(char, passsize);

  /* Turn echoing off and fail if we can't. */
  if (tcgetattr(fileno(stdin), &orig) != 0) return NULL;
  backup_termios = &orig;

  new = orig;
  new.c_lflag &= ~ECHO;
  if (tcsetattr(fileno(stdin), TCSAFLUSH, &new) != 0) return NULL;

  /* Read the password. */
  printf("Please enter %s: ", what);
  if (fgets(password, passsize, stdin) == NULL) return NULL;

  /* Restore terminal. */
  tcsetattr(fileno(stdin), TCSAFLUSH, &orig);
  printf("\n");
  backup_termios = NULL;

  for (p = (char*)password; *p; p++)
    ;
  for ( ; p > (char*)password ; p--)
    if (*p == '\n' || *p == '\r') *p = 0;

  return password;
}

//  password_eval(command, *status)
// Get password from a system command.
// The string must be freed after use.
static char *password_eval(const char *command, int *status)
{
#define MAX_PWD 100
  char *pwd;
  FILE *outfp = popen(command, "r");
  if (outfp == NULL) {
    scr_log_print(LPRINT_NORMAL,
                  "** ERROR: Failed to execute password_eval command.");
    *status = -1;
    return NULL;
  }

  pwd = g_new0(char, MAX_PWD);
  if (fgets(pwd, MAX_PWD, outfp) == NULL) {
    scr_log_print(LPRINT_NORMAL,
                  "** ERROR: Failed to read from password_eval command.");
    g_free(pwd);
    *status = -1;
    return NULL;
  }

  int res = pclose(outfp);
  if (res != 0 && errno != ECHILD) {
    scr_log_print(LPRINT_NORMAL,
                  "** ERROR: Password evaluation command exited with error %d.",
                  res);
    if (res == -1) {
      scr_log_print(LPRINT_NORMAL, "   errno=%d", errno);
    }
    g_free(pwd);
    *status = res;
    return NULL;
  }

  // Strip trailing whitespaces and newlines
  size_t i = strlen(pwd);
  while (i && isspace(pwd[i-1])) {
    i--;
  }
  pwd[i] = '\0';
  return pwd;
}

static void credits(void)
{
  const char *v_fmt = "MCabber %s -- Email: mcabber [at] lilotux [dot] net\n";
  char *v = mcabber_version();
  printf(v_fmt, v);
  scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8, v_fmt, v);
  g_free(v);
}

static void compile_options(void)
{
  puts("Installation data directory: " DATA_DIR "\n");
#ifdef HAVE_UNICODE
  puts("Compiled with unicode support.");
#endif
#ifdef MODULES_ENABLE
  printf("Compiled with modules support. (API %s:%d-%d)\n",
         MCABBER_BRANCH, MCABBER_API_MIN, MCABBER_API_VERSION);
# ifdef PKGLIB_DIR
  puts("Modules directory: " PKGLIB_DIR);
# endif
#endif
#ifdef HAVE_GPGME
  puts("Compiled with GPG support.");
#endif
#ifdef HAVE_LIBOTR
  puts("Compiled with OTR support.");
#endif
#ifdef WITH_ENCHANT
  puts("Compiled with Enchant support.");
#endif
#ifdef WITH_ASPELL
  puts("Compiled with Aspell support.");
#endif
#ifdef ENABLE_DEBUG
  puts("Compiled with debugging support.");
#endif
}

static void main_init_pgp(void)
{
#ifdef HAVE_GPGME
  const char *pk, *pp;
  char *typed_passwd = NULL;
  char *p;
  bool pgp_invalid = FALSE;
  bool pgp_agent;
  int retries;

  pk = settings_opt_get("pgp_private_key");

  if (!pk)
    scr_LogPrint(LPRINT_LOGNORM, "WARNING: unknown PGP private key");

  if (gpg_init(pk, NULL)) {
    scr_LogPrint(LPRINT_LOGNORM, "WARNING: Could not initialize PGP.");
    return;
  }

  // We're done if the PGP engine version is > 1
  // since the agent is mandatory and password mechanism is external.
  if (!gpg_is_version1())
    return;


  p = getenv("GPG_AGENT_INFO");
  pgp_agent = (p && strchr(p, ':'));

  if (settings_opt_get("pgp_passphrase_retries"))
    retries = settings_opt_get_int("pgp_passphrase_retries");
  else
    retries = 2;

  pp = settings_opt_get("pgp_passphrase");

  if (!pk) {
    pgp_invalid = TRUE;
  } else if (!(pp || pgp_agent)) {
    // Request PGP passphrase
    pp = typed_passwd = ask_password("your PGP passphrase");
  }
  gpg_set_passphrase(pp);
  // Erase password from the settings array
  if (pp) {
    memset((char*)pp, 0, strlen(pp));
    if (typed_passwd)
      g_free(typed_passwd);
    else
      settings_set(SETTINGS_TYPE_OPTION, "pgp_passphrase", NULL);
  }
  if (!pgp_agent && pk && pp && gpg_test_passphrase()) {
    // Let's check the pasphrase
    int i;
    for (i = 1; retries < 0 || i <= retries; i++) {
      typed_passwd = ask_password("your PGP passphrase"); // Ask again...
      if (typed_passwd) {
        gpg_set_passphrase(typed_passwd);
        memset(typed_passwd, 0, strlen(typed_passwd));
        g_free(typed_passwd);
      }
      if (!gpg_test_passphrase())
        break; // Ok
    }
    if (i > retries)
      pgp_invalid = TRUE;
  }
  if (pgp_invalid)
    scr_LogPrint(LPRINT_LOGNORM, "WARNING: PGP key/pass invalid");
#else /* not HAVE_GPGME */
  scr_LogPrint(LPRINT_LOGNORM, "WARNING: not compiled with PGP support");
#endif /* HAVE_GPGME */
}

void mcabber_set_terminate_ui(void)
{
  terminate_ui = TRUE;
}

typedef struct {
  GSource source;
  GPollFD pollfd;
} mcabber_source_t;

static gboolean mcabber_source_prepare(GSource *source, gint *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean mcabber_source_check(GSource *source)
{
  mcabber_source_t *mc_source = (mcabber_source_t *) source;
  gushort revents = mc_source->pollfd.revents;
  if (revents)
    return TRUE;
  return FALSE;
}

static gboolean keyboard_activity(void)
{
  keycode kcode;

  if (terminate_ui) {
    return FALSE;
  }
  scr_do_update();
  scr_getch(&kcode);

  while (kcode.value != ERR) {
    scr_process_key(kcode);
    scr_getch(&kcode);
  }
  scr_check_auto_away(FALSE);

  return TRUE;
}

static gboolean mcabber_source_dispatch(GSource *source, GSourceFunc callback,
                                        gpointer udata) {
  return keyboard_activity();
}

static GSourceFuncs mcabber_source_funcs = {
  mcabber_source_prepare,
  mcabber_source_check,
  mcabber_source_dispatch,
  NULL,
  NULL,
  NULL
};

int main(int argc, char **argv)
{
  char *configFile = NULL;
  const char *optstring;
  int optval, optval2;
  int ret;

  credits();

  signal(SIGTERM, sig_handler);
  signal(SIGINT,  sig_handler);
  signal(SIGHUP,  sig_handler);
  signal(SIGCHLD, sig_handler);
#ifdef USE_SIGWINCH
  signal(SIGWINCH, sig_handler);
#endif
  signal(SIGPIPE, SIG_IGN);

  /* Parse command line options */
  while (1) {
    int c = getopt(argc, argv, "hVf:");
    if (c == -1) {
      break;
    } else
      switch (c) {
      case 'h':
      case '?':
        printf("Usage: %s [-h|-V|-f mcabberrc_file]\n\n", argv[0]);
        return (c == 'h' ? 0 : -1);
      case 'V':
        compile_options();
        return 0;
      case 'f':
        configFile = g_strdup(optarg);
        break;
      }
  }

  if (optind < argc) {
    fprintf(stderr, "Usage: %s [-h|-V|-f mcabberrc_file]\n\n", argv[0]);
    return -1;
  }

  /* Initialize command system, roster and default key bindings */
  compl_init_system();
  cmd_init();
  roster_init();
  settings_init();
  scr_init_bindings();
  scr_init_settings();
  caps_init();
#ifdef MODULES_ENABLE
  modules_init();
#endif
  /* Initialize charset */
  scr_init_locale_charset();
  ut_init_debug();
  help_init();

  /* Parsing config file... */
  ret = cfg_read_file(configFile, TRUE);
  /* free() configFile if it has been allocated during options parsing */
  g_free(configFile);
  /* Leave if there was an error in the config. file */
  if (ret == -2)
    exit(EXIT_FAILURE);

  /* Display configuration settings */
  {
    const char *p;
    if ((p = settings_opt_get("server")) != NULL)
      scr_log_print(LPRINT_NORMAL, "Server: %s", p);
    if ((p = settings_opt_get("jid")) != NULL) {
      scr_log_print(LPRINT_NORMAL, "User JID: %s", p);
    }
  }

  /* If no password is stored, we ask for it before entering
     ncurses mode -- unless the username is unknown. */
  if (settings_opt_get("jid") && !settings_opt_get("password")) {
    const char *pass_eval = settings_opt_get("password_eval");
    if (pass_eval) {
      int status = 0;
      char *pwd = password_eval(pass_eval, &status);
      if (status == 0 && pwd) {
        settings_set(SETTINGS_TYPE_OPTION, "password", pwd);
      }
      g_free(pwd);
    }
    // If the password is still unset, ask the user...
    if (!settings_opt_get("password")) {
      char *pwd = ask_password("your Jabber password");
      settings_set(SETTINGS_TYPE_OPTION, "password", pwd);
      g_free(pwd);
    }
  }

  /* Initialize PGP system
     We do it before ncurses initialization because we may need to request
     a passphrase. */
  if (settings_opt_get_int("pgp"))
    main_init_pgp();

  /* Initialize N-Curses */
  scr_LogPrint(LPRINT_DEBUG, "Initializing N-Curses...");
  scr_init_curses();
  scr_draw_main_window(TRUE);

  optval   = (settings_opt_get_int("logging") > 0);
  optval2  = (settings_opt_get_int("load_logs") > 0);
  if (optval || optval2)
    hlog_enable(optval, settings_opt_get("logging_dir"), optval2);

#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
  /* Initialize spelling */
  if (settings_opt_get_int("spell_enable")) {
    spellcheck_init();
  }
#endif

  optstring = settings_opt_get("events_command");
  if (optstring)
    hk_ext_cmd_init(optstring);

  optstring = settings_opt_get("roster_display_filter");
  if (optstring)
    scr_roster_display(optstring);
  // Empty filter isn't allowed...
  if (!buddylist_get_filter())
    scr_roster_display("*");

  chatstates_disabled = settings_opt_get_int("disable_chatstates");

#ifndef MODULES_ENABLE
  /* Initialize FIFO named pipe */
  fifo_init();
#endif

  /* Load previous roster state */
  hlog_load_state();

  main_context = g_main_context_default();

  if (ret < 0) {
    scr_LogPrint(LPRINT_NORMAL, "No configuration file has been found.");
    scr_show_buddy_window();
  } else {
    /* Connection */
    if (xmpp_connect())
      scr_show_buddy_window();
  }

  // Initial drawing
  scr_draw_roster();
  scr_do_update();

  { // add keypress processing source
    GSource *mc_source = g_source_new(&mcabber_source_funcs,
                                      sizeof(mcabber_source_t));
    GPollFD *mc_pollfd = &(((mcabber_source_t *)mc_source)->pollfd);
    mc_pollfd->fd = STDIN_FILENO;
    mc_pollfd->events = POLLIN|POLLERR|POLLPRI;
    mc_pollfd->revents = 0;
    g_source_add_poll(mc_source, mc_pollfd);
    g_source_attach(mc_source, main_context);

    scr_LogPrint(LPRINT_DEBUG, "Entering into main loop...");

    while(!terminate_ui) {
      if (g_main_context_iteration(main_context, TRUE) == FALSE)
        keyboard_activity();
#ifdef USE_SIGWINCH
      if (sigwinch) {
        sigwinch_resize();
        sigwinch = FALSE;
      }
#endif
      if (update_roster)
        scr_draw_roster();
      scr_do_update();
    }

    g_source_destroy(mc_source);
    g_source_unref(mc_source);
  }

  evs_deinit();
#ifdef MODULES_ENABLE
  modules_deinit();
#endif
#ifndef MODULES_ENABLE
  fifo_deinit();
#endif
#ifdef HAVE_LIBOTR
  otr_terminate();
#endif
  xmpp_disconnect();
#ifdef HAVE_GPGME
  gpg_terminate();
#endif
#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
  /* Deinitialize spelling */
  if (settings_opt_get_int("spell_enable"))
    spellcheck_deinit();
#endif

  scr_terminate_curses();
  /* Save pending message state */
  hlog_save_state();
  caps_free();

  printf("\n\nThanks for using mcabber!\n");

  return 0;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
