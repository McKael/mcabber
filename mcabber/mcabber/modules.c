/*
 * modules.c   -- modules handling
 *
 * Copyright (C) 2010 Myhailo Danylenko <isbear@ukrpost.net>
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

#include <glib.h>
#include <gmodule.h>
#include <string.h>

#include "settings.h"
#include "config.h"
#include "modules.h"
#include "logprint.h"
#include "utils.h"

// Registry of loaded modules
GSList *loaded_modules = NULL;

const gchar *mcabber_branch = MCABBER_BRANCH;
const guint mcabber_api_version = MCABBER_API_VERSION;

static gint module_list_comparator(gconstpointer arg1, gconstpointer arg2)
{
  const loaded_module_t *module = arg1;
  const char *name = arg2;
  return g_strcmp0(module->name, name);
}

//  module_load(modulename, manual, force)
// Tries to load specified module and any modules, that this module
// depends on. Returns NULL on success or constant error string in a
// case of error. Error message not necessarily indicates error.
const gchar *module_load(const gchar *arg, gboolean manual, gboolean force)
{
  GModule       *mod;
  module_info_t *info;

  if (!arg || !*arg)
    return "Missing module name";

  { // Check if module is already loaded
    GSList *lmod = g_slist_find_custom(loaded_modules, arg,
                                       module_list_comparator);

    if (lmod) {
      loaded_module_t *module = lmod->data;

      if (manual) {
        if (!module->locked) {
          module->locked = TRUE;
          module->refcount += 1;
          return force ? NULL : "Module is already automatically loaded, "
                                "marked as manually loaded";
        } else
          return force ? NULL : "Module is already loaded";
      } else {
        module->refcount += 1;
        return NULL;
      }
    }
  }

  { // Load module
    gchar *mdir = expand_filename(settings_opt_get("modules_dir"));
    gchar *path = g_module_build_path(mdir ? mdir : PKGLIB_DIR, arg);
    g_free(mdir);
    mod = g_module_open(path, G_MODULE_BIND_LAZY);
    g_free(path);
    if (!mod)
      return g_module_error();
  }

  { // Obtain module information structures list
    gchar *varname = g_strdup_printf("info_%s", arg);
    gpointer var = NULL;

    // convert to a valid symbol name
    g_strcanon(varname, "abcdefghijklmnopqrstuvwxyz0123456789", '_');

    if (!g_module_symbol(mod, varname, &var)) {
      if (!force) {
        g_free(varname);
        if(!g_module_close(mod))
          scr_LogPrint(LPRINT_LOGNORM, "Error closing module: %s.",
                       g_module_error());
        return "Module provides no information structure";
      }

      scr_LogPrint(LPRINT_LOGNORM, "Forced to ignore error: "
                                 "Module provides no information structure.");
    }

    g_free(varname);
    info = var;
  }

  // Find appropriate info struct
  if (info) {
    while (info) {
      if (!info->branch || !*(info->branch)) {
        scr_LogPrint(LPRINT_DEBUG, "No branch name, "
                     "skipping info chunk.");
      } else if (strcmp(info->branch, mcabber_branch)) {
        scr_LogPrint(LPRINT_DEBUG, "Unhandled branch %s, "
                     "skipping info chunk.", info->branch);
      } else if (info->api > mcabber_api_version ||
                 info->api < MCABBER_API_MIN) { // XXX force?
        if(!g_module_close(mod))
          scr_LogPrint(LPRINT_LOGNORM, "Error closing module: %s.",
                       g_module_error());
        return "Incompatible mcabber api version";
      } else
        break;
      info = info->next;
    }

    if (!info) { // XXX force?
      if(!g_module_close(mod))
        scr_LogPrint(LPRINT_LOGNORM, "Error closing module: %s.",
                     g_module_error());
      return "No supported mcabber branch description found";
    }
  }

  // Load dependencies
  if (info && info->requires) {
    const gchar **dep;
    GSList *deps = NULL;

    for (dep = info->requires; *dep; ++dep) {
      const gchar *err = module_load(*dep, FALSE, FALSE);

      if (err) {
        GSList *mel;
        scr_LogPrint(LPRINT_LOGNORM, "Error loading dependency module %s: %s.",
                     *dep, err);

        // Unload already loaded dependencies
        for (mel = deps; mel; mel = mel->next) {
          gchar *ldmname = mel->data;
          err = module_unload(ldmname, FALSE, FALSE);
          scr_LogPrint(LPRINT_LOGNORM,
                       "Error unloading dependency module %s: %s.",
                       ldmname, err);
          g_free(ldmname);
        }
        g_slist_free(deps);

        // Unload module
        if (!g_module_close(mod))
          scr_LogPrint(LPRINT_LOGNORM, "Error unloading module %s: %s.",
                       arg, g_module_error());
        return "Dependency problems";
      }

      deps = g_slist_append(deps, (gpointer) *dep);
    }

    g_slist_free(deps);
  }

  { // Register module
    loaded_module_t *module = g_new(loaded_module_t, 1);

    module->refcount     = 1;
    module->locked       = manual;
    module->name         = g_strdup(arg);
    module->module       = mod;
    module->info         = info;

    loaded_modules = g_slist_prepend(loaded_modules, module);
  }

  // Run initialization routine
  if (info && info->init)
    info->init();

  // XXX Run hk_loaded_module hook (and move this line there)
  scr_LogPrint(LPRINT_LOGNORM, "Loaded module %s.", arg);

  return NULL;
}

//  module_unload(modulename, manual, force)
// Unload specified module and any automatically loaded modules
// that are no more required.
const gchar *module_unload(const gchar *arg, gboolean manual, gboolean force)
{
  GSList          *lmod;
  loaded_module_t *module;
  module_info_t   *info;

  if (!arg || !*arg)
    return "Missing module name";

  lmod = g_slist_find_custom(loaded_modules, arg, module_list_comparator);
  if (!lmod)
    return "Module not found";

  module = lmod->data;

  // Check if user can unload this module
  if (manual) {
    if (!module->locked) {
      if (force)
        scr_LogPrint(LPRINT_LOGNORM, "Forced to ignore error: "
                     "Manually unloading automatically loaded module.");
      else
        return "Module is not loaded manually";
    }
    module->locked = FALSE;
  }

  // Check refcount
  module->refcount -= 1;
  if (module->refcount > 0) {
    if (force)
      scr_LogPrint(LPRINT_LOGNORM, "Forced to ignore error: "
                   "Refcount is not zero (%u).", module->refcount);
    else
      return manual ? "Module is required by some other modules" : NULL;
  }

  info = module->info;

  // Run uninitialization routine
  if (info && info->uninit)
    info->uninit();

  // Unload dependencies
  if (info && info->requires) {
    const gchar **dep;
    for (dep = info->requires; *dep; ++dep) {
      const gchar *err = module_unload(*dep, FALSE, FALSE);
      if (err) // XXX
        scr_LogPrint(LPRINT_LOGNORM,
                     "Error unloading automatically loaded module %s: %s.",
                     *dep, err);
    }
  }

  // XXX Prevent uninitialization routine and dep unloading to be performed again
  module->info = NULL;

  // Unload module
  if (!g_module_close(module->module))
    return g_module_error(); // XXX destroy structure?

  // Destroy structure
  loaded_modules = g_slist_delete_link(loaded_modules, lmod);
  g_free(module->name);
  g_free(module);

  scr_LogPrint(LPRINT_LOGNORM, "Unloaded module %s.", arg);

  return NULL;
}

//  module_list_print(void)
// Prints into status buffer and log list of the currently loaded
// modules.
void module_list_print(void)
{
  GSList *mel;
  gsize maxlen = 0;
  gchar *format;
  GString *message;

  if (!loaded_modules) {
    scr_LogPrint(LPRINT_LOGNORM, "No modules loaded.");
    return;
  }

  // Count maximum module name length
  for (mel = loaded_modules; mel; mel = mel -> next) {
    loaded_module_t *module = mel->data;
    gsize len = strlen(module->name);
    if (len > maxlen)
      maxlen = len;
  }

  // Create format string
  format = g_strdup_printf("%%-%us  %%2u (%%c)", maxlen);

  // Fill the message to be printed
  message = g_string_new("Loaded modules:\n");
  for (mel = loaded_modules; mel; mel = mel -> next) {
    loaded_module_t *module = mel->data;

    g_string_append_printf(message, format, module->name, module->refcount,
                           module->locked ? 'M' : 'A');

    if (module->info) {
      module_info_t *info = module->info;

      // Module version
      if (info->version) {
        g_string_append(message, " version: ");
        g_string_append(message, info->version);
      }

      // Module dependencies
      if (info->requires && *(info->requires)) {
        const gchar **dep;
        g_string_append(message, " depends: ");

        for (dep = info->requires; *dep; ++dep) {
          g_string_append(message, *dep);
          g_string_append(message, ", ");
        }

        // Chop extra ", "
        g_string_truncate(message, message->len - 2);
      }
    }

    g_string_append_c(message, '\n');
  }

  // Chop extra "\n"
  g_string_truncate(message, message->len - 1);

  scr_LogPrint(LPRINT_LOGNORM, "%s", message->str);

  g_string_free(message, TRUE);
  g_free(format);
}

//  module_info_print(name)
// Prints info about specific module
void module_info_print(const gchar *name)
{
  GSList *lmod;
  loaded_module_t *module;
  module_info_t *info;

  if (!name || !name[0]) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a module name.");
    return;
  }

  lmod = g_slist_find_custom(loaded_modules, name, module_list_comparator);
  if (!lmod) {
    scr_LogPrint(LPRINT_NORMAL, "Module %s not found.", name);
    return;
  }

  module = lmod->data;
  info = module->info;

  scr_LogPrint(LPRINT_NORMAL, "Name: %s", module->name);
  scr_LogPrint(LPRINT_NORMAL, "Location: %s", g_module_name(module->module));
  scr_LogPrint(LPRINT_NORMAL, "Loaded: %s",
               module->locked ? "Manually" : "Automatically");
  scr_LogPrint(LPRINT_NORMAL, "Reference count: %u", module->refcount);

  if (info) {

    if (info->version)
      scr_LogPrint(LPRINT_NORMAL, "Version: %s", info->version);

    if (info->requires && *(info->requires)) {
      GString *message = g_string_new("Depends on: ");
      const gchar **dep;
      for (dep = info->requires; *dep; ++dep) {
        g_string_append(message, *dep);
        g_string_append(message, ", ");
      }

      // Chop last ", "
      g_string_truncate(message, message->len - 2);
      scr_LogPrint(LPRINT_NORMAL, "%s", message->str);
      g_string_free(message, TRUE);
    }

    if (info->description)
      scr_LogPrint(LPRINT_NORMAL, "Description: %s", info->description);
  }
}

//  modules_init()
// Initializes module system.
void modules_init(void)
{
}

//  modules_deinit()
// Unloads all the modules.
void modules_deinit(void)
{
  GSList *mel;

  // We need only manually loaded modules
  for (mel = loaded_modules; mel; mel = mel->next) {
    loaded_module_t *module = mel->data;
    if (module->locked)
      break;
  }

  while (mel) {
    loaded_module_t *module = mel->data;
    const gchar     *err;

    // Find next manually loaded module to treat
    for (mel = mel->next; mel; mel = mel->next) {
      loaded_module_t *module = mel->data;
      if (module->locked)
        break;
    }

    // Unload module
    scr_LogPrint(LPRINT_LOGNORM, "Unloading module %s.", module->name);
    err = module_unload(module->name, TRUE, FALSE);
    if (err)
      scr_LogPrint(LPRINT_LOGNORM, "* Module unloading failed: %s.", err);
  }
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
