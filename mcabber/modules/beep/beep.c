
/* Copyright 2009 Myhailo Danylenko

This file is part of mcabber module writing howto examples.

Examples are free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <string.h>

#include <mcabber/logprint.h>
#include <mcabber/commands.h>
#include <mcabber/compl.h>
#include <mcabber/hooks.h>
#include <mcabber/screen.h>
#include <mcabber/settings.h>
#include <mcabber/modules.h>

static void beep_init   (void);
static void beep_uninit (void);

/* Module description */
module_info_t info_beep = {
	.mcabber_version = "0.10.0",
	.requires        = NULL,
	.init            = beep_init,
	.uninit          = beep_uninit,
};

static guint beep_cid = 0;

/* Event handler */
static void beep_hh (guint32 hid, hk_arg_t *args, gpointer userdata)
{
	/* Check if beeping is enabled */
	if (settings_opt_get_int ("beep_enable"))
		/* *BEEP*! */
		scr_Beep ();
}

/* beep command handler */
static void do_beep (char *args)
{
	/* Check arguments, and if recognized,
	 * set mcabber option accordingly */
	if (!strcmp (args, "enable") ||
	    !strcmp (args, "on") ||
	    !strcmp (args, "yes") ||
	    !strcmp (args, "1"))
		settings_set (SETTINGS_TYPE_OPTION,
		              "beep_enable", "1");
	else if (!strcmp (args, "disable") ||
		 !strcmp (args, "off") ||
		 !strcmp (args, "no") ||
		 !strcmp (args, "0"))
		settings_set (SETTINGS_TYPE_OPTION,
		              "beep_enable", "0");

	/* Output current state, either if state is
	 * changed and if argument is not recognized */
	if (settings_opt_get_int ("beep_enable"))
		scr_LogPrint (LPRINT_NORMAL,
		              "Beep on messages is enabled");
	else
		scr_LogPrint (LPRINT_NORMAL,
		              "Beep on messages is disabled");
}

/* Initialization */
static void beep_init (void)
{
	/* Create completions */
	beep_cid = compl_new_category ();
	if (beep_cid) {
		compl_add_category_word (beep_cid, "enable");
		compl_add_category_word (beep_cid, "disable");
	}
	/* Add command */
	cmd_add ("beep", "", beep_cid, 0, do_beep, NULL);
	/* Add handler
	 * We are only interested in incoming message events
	 */
	hk_add_handler (beep_hh, HOOK_MESSAGE_IN, NULL);
}

/* Deinitialization */
static void beep_uninit (void)
{
	/* Unregister event handler */
	hk_del_handler (beep_hh, NULL);
	/* Unregister command */
	cmd_del ("beep");
	/* Give back completion handle */
	if (beep_cid)
		compl_del_category (beep_cid);
}

/* The End */
