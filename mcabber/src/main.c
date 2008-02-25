/*
 * main.c
 *
 * Copyright (C) 2005-2008 Mikael Berthe <mikael@lilotux.net>
 * Parts of this file come from Cabber <cabber@ajmacias.com>
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
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>
#include <config.h>

#include "jabglue.h"
#include "screen.h"
#include "settings.h"
#include "roster.h"
#include "commands.h"
#include "histolog.h"
#include "hooks.h"
#include "utils.h"
#include "pgp.h"
#include "otr.h"

#ifdef ENABLE_FIFO
# include "fifo.h"
#endif

#ifdef ENABLE_HGCSET
# include "hgcset.h"
#endif

#ifndef WAIT_ANY
# define WAIT_ANY -1
#endif

static unsigned int terminate_ui;

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

void mcabber_connect(void)
{
  const char *username, *password, *resource, *servername;
  const char *proxy_host;
  char *dynresource = NULL;
  char *fjid;
  int ssl;
  int sslverify = -1;
  const char *sslvopt = NULL, *cafile = NULL, *capath = NULL, *ciphers = NULL;
  static char *cafile_xp, *capath_xp;
  unsigned int port;

  servername = settings_opt_get("server");
  username   = settings_opt_get("username");
  password   = settings_opt_get("password");
  resource   = settings_opt_get("resource");
  proxy_host = settings_opt_get("proxy_host");

  // Free the ca*_xp strings if they've already been set.
  g_free(cafile_xp);
  g_free(capath_xp);
  cafile_xp = capath_xp = NULL;

  if (!servername) {
    scr_LogPrint(LPRINT_NORMAL, "Server name has not been specified!");
    return;
  }
  if (!username) {
    scr_LogPrint(LPRINT_NORMAL, "User name has not been specified!");
    return;
  }
  if (!password) {
    scr_LogPrint(LPRINT_NORMAL, "Password has not been specified!");
    return;
  }

  port    = (unsigned int) settings_opt_get_int("port");

  ssl     = settings_opt_get_int("ssl");
  sslvopt = settings_opt_get("ssl_verify");
  if (sslvopt)
    sslverify = settings_opt_get_int("ssl_verify");
  cafile  = settings_opt_get("ssl_cafile");
  capath  = settings_opt_get("ssl_capath");
  ciphers = settings_opt_get("ssl_ciphers");

#if !defined(HAVE_OPENSSL) && !defined(HAVE_GNUTLS)
  if (ssl) {
    scr_LogPrint(LPRINT_LOGNORM, "** Error: SSL is NOT available, "
                 "do not set the option 'ssl'.");
    return;
  } else if (sslvopt || cafile || capath || ciphers) {
    scr_LogPrint(LPRINT_LOGNORM, "** Warning: SSL is NOT available, "
                 "ignoring ssl-related settings");
    ssl = sslverify = 0;
    cafile = capath = ciphers = NULL;
  }
#elif defined HAVE_GNUTLS
  if (sslverify != 0) {
    scr_LogPrint(LPRINT_LOGNORM, "** Error: SSL certificate checking "
                 "is not supported yet with GnuTLS.");
    scr_LogPrint(LPRINT_LOGNORM,
                 " * Please set 'ssl_verify' to 0 explicitly!");
    return;
  }
#endif
  cafile_xp = expand_filename(cafile);
  capath_xp = expand_filename(capath);
  cw_set_ssl_options(sslverify, cafile_xp, capath_xp, ciphers, servername);
  // We can't free the ca*_xp variables now, because they're not duplicated
  // in cw_set_ssl_options().

  if (!resource) {
    unsigned int tab[2];
    srand(time(NULL));
    tab[0] = (unsigned int) (0xffff * (rand() / (RAND_MAX + 1.0)));
    tab[1] = (unsigned int) (0xffff * (rand() / (RAND_MAX + 1.0)));
    dynresource = g_strdup_printf("%s.%04x%04x", PACKAGE_NAME,
                                  tab[0], tab[1]);
    resource = dynresource;
  }

  /* Connect to server */
  scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, "Connecting to server: %s",
               servername);
  if (port)
    scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, " using port %d", port);
  scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, " resource %s", resource);

  if (proxy_host) {
    int proxy_port = settings_opt_get_int("proxy_port");
    if (proxy_port <= 0 || proxy_port > 65535) {
      scr_LogPrint(LPRINT_LOGNORM, "Invalid proxy port: %d", proxy_port);
    } else {
      const char *proxy_user, *proxy_pass;
      proxy_user = settings_opt_get("proxy_user");
      proxy_pass = settings_opt_get("proxy_pass");
      // Proxy initialization
      cw_setproxy(proxy_host, proxy_port, proxy_user, proxy_pass);
      scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, " using proxy %s:%d",
                   proxy_host, proxy_port);
    }
  }

  fjid = compose_jid(username, servername, resource);
#if defined(HAVE_LIBOTR)
  otr_init(fjid);
#endif
  jc = jb_connect(fjid, servername, port, ssl, password);
  g_free(fjid);
  g_free(dynresource);

  if (!jc)
    scr_LogPrint(LPRINT_LOGNORM, "Error connecting to (%s)", servername);

  jb_reset_keepalive();
}

static void mcabber_terminate(const char *msg)
{
  jb_disconnect();
  scr_TerminateCurses();

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
            scr_Beep();
          }
        }
      }
    } while (pid > 0);
    signal(SIGCHLD, sig_handler);
  } else if (signum == SIGTERM) {
    mcabber_terminate("Killed by SIGTERM");
  } else if (signum == SIGINT) {
    mcabber_terminate("Killed by SIGINT");
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
  fgets(password, passsize, stdin);

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

static void credits(void)
{
  const char *v_fmt = "MCabber %s -- Email: mcabber [at] lilotux [dot] net\n";
  char *v = mcabber_version();
  printf(v_fmt, v);
  scr_LogPrint(LPRINT_LOGNORM|LPRINT_NOTUTF8, v_fmt, v);
  g_free(v);
}

static void compile_options(void)
{
  puts("Installation data directory: " DATA_DIR "\n");
#ifdef HAVE_UNICODE
  puts("Compiled with unicode support.");
#endif
#ifdef HAVE_OPENSSL
  puts("Compiled with OpenSSL support.");
#elif defined HAVE_GNUTLS
  puts("Compiled with GnuTLS support.");
#endif
#ifdef HAVE_GPGME
  puts("Compiled with GPG support.");
#endif
#ifdef HAVE_LIBOTR
  puts("Compiled with OTR support.");
#endif
#ifdef WITH_ASPELL
  puts("Compiled with Aspell support.");
#endif
#ifdef ENABLE_FIFO
  puts("Compiled with FIFO support.");
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

  p = getenv("GPG_AGENT_INFO");
  pgp_agent = (p && strchr(p, ':'));

  pk = settings_opt_get("pgp_private_key");
  pp = settings_opt_get("pgp_passphrase");

  if (settings_opt_get("pgp_passphrase_retries"))
    retries = settings_opt_get_int("pgp_passphrase_retries");
  else
    retries = 2;

  if (!pk) {
    scr_LogPrint(LPRINT_LOGNORM, "WARNING: unkown PGP private key");
    pgp_invalid = TRUE;
  } else if (!(pp || pgp_agent)) {
    // Request PGP passphrase
    pp = typed_passwd = ask_password("PGP passphrase");
  }
  gpg_init(pk, pp);
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
      typed_passwd = ask_password("PGP passphrase"); // Ask again...
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

int main(int argc, char **argv)
{
  char *configFile = NULL;
  const char *optstring;
  int optval, optval2;
  unsigned int ping;
  int ret;
  keycode kcode;

  credits();

  signal(SIGTERM, sig_handler);
  signal(SIGINT,  sig_handler);
  signal(SIGCHLD, sig_handler);
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
  cmd_init();
  roster_init();
  settings_init();
  scr_init_bindings();
  /* Initialize charset */
  scr_InitLocaleCharSet();

  /* Parsing config file... */
  ret = cfg_read_file(configFile, TRUE);
  /* free() configFile if it has been allocated during options parsing */
  g_free(configFile);
  /* Leave if there was an error in the config. file */
  if (ret == -2)
    exit(EXIT_FAILURE);

  optstring = settings_opt_get("tracelog_file");
  if (optstring)
    ut_InitDebug(settings_opt_get_int("tracelog_level"), optstring);

  /* If no password is stored, we ask for it before entering
     ncurses mode -- unless the username is unknown. */
  if (settings_opt_get("username") && !settings_opt_get("password")) {
    const char *p;
    char *pwd;
    p = settings_opt_get("server");
    if (p)
      printf("Server: %s\n", p);
    p = settings_opt_get("username");
    if (p)
      printf("Username: %s\n", p);

    pwd = ask_password("Jabber password");
    settings_set(SETTINGS_TYPE_OPTION, "password", pwd);
    g_free(pwd);
  }

  /* Initialize PGP system
     We do it before ncurses initialization because we may need to request
     a passphrase. */
  if (settings_opt_get_int("pgp"))
    main_init_pgp();

  /* Initialize N-Curses */
  scr_LogPrint(LPRINT_DEBUG, "Initializing N-Curses...");
  scr_InitCurses();
  scr_DrawMainWindow(TRUE);

  optval   = (settings_opt_get_int("logging") > 0);
  optval2  = (settings_opt_get_int("load_logs") > 0);
  if (optval || optval2)
    hlog_enable(optval, settings_opt_get("logging_dir"), optval2);

#ifdef HAVE_ASPELL_H
  /* Initialize aspell */
  if (settings_opt_get_int("aspell_enable")) {
    spellcheck_init();
  }
#endif

  optstring = settings_opt_get("events_command");
  if (optstring)
    hk_ext_cmd_init(optstring);

  ping = 40;
  if (settings_opt_get("pinginterval"))
    ping = (unsigned int) settings_opt_get_int("pinginterval");
  jb_set_keepalive_delay(ping);
  scr_LogPrint(LPRINT_DEBUG, "Ping interval established: %d secs", ping);

  if (settings_opt_get_int("hide_offline_buddies") > 0) { // XXX Deprecated
    scr_RosterDisplay("ofdna");
    scr_LogPrint(LPRINT_LOGNORM,
                 "* Warning: 'hide_offline_buddies' is deprecated.");
  } else {
    optstring = settings_opt_get("roster_display_filter");
    if (optstring)
      scr_RosterDisplay(optstring);
    // Empty filter isn't allowed...
    if (!buddylist_get_filter())
      scr_RosterDisplay("*");
  }

  chatstates_disabled = settings_opt_get_int("disable_chatstates");

#ifdef ENABLE_FIFO
  /* Initialize FIFO named pipe */
  fifo_init(settings_opt_get("fifo_name"));
#endif

  /* Load previous roster state */
  hlog_load_state();

  if (ret < 0) {
    scr_LogPrint(LPRINT_NORMAL, "No configuration file has been found.");
    scr_ShowBuddyWindow();
  } else {
    /* Connection */
    mcabber_connect();
  }

  scr_LogPrint(LPRINT_DEBUG, "Entering into main loop...");

  while (!terminate_ui) {
    scr_DoUpdate();
    scr_Getch(&kcode);

    if (kcode.value != ERR) {
      process_key(kcode);
    } else {
      scr_CheckAutoAway(FALSE);

      if (update_roster)
        scr_DrawRoster();

      jb_main();
      hk_mainloop();
    }
  }

  scr_TerminateCurses();
#ifdef ENABLE_FIFO
  fifo_deinit();
#endif
#ifdef HAVE_LIBOTR
  otr_terminate();
#endif
  jb_disconnect();
#ifdef HAVE_GPGME
  gpg_terminate();
#endif
#ifdef HAVE_ASPELL_H
  /* Deinitialize aspell */
  if (settings_opt_get_int("aspell_enable"))
    spellcheck_deinit();
#endif
  /* Save pending message state */
  hlog_save_state();

  printf("\n\nThanks for using mcabber!\n");

  return 0;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
