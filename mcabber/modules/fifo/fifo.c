/*
 *  Module "fifo"       -- Reads and executes command from FIFO pipe
 *
 * Copyright 2009, 2010 Myhailo Danylenko
 *
 * This file is part of mcabber.
 *
 * mcabber is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <glib.h>
#include <gmodule.h>

#include <mcabber/fifo.h>
#include <mcabber/modules.h>
#include <mcabber/config.h>

module_info_t info_fifo = {
  .branch          = MCABBER_BRANCH,
  .api             = MCABBER_API_VERSION,
  .version         = MCABBER_VERSION,
  .requires        = NULL,
  .init            = NULL,
  .uninit          = NULL,
  .description     = "Reads and executes command from FIFO pipe\n"
          "Recognizes options fifo_name (required), fifo_hide_commands and fifo_ignore.",
  .next            = NULL,
};

gchar *g_module_check_init(GModule *module)
{
  if (fifo_init() == -1)
    return "FIFO initialization failed";
  else
    return NULL;
}

void g_module_unload(GModule *module)
{
  fifo_deinit();
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
