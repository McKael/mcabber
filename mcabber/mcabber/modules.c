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

// Information about loaded module
typedef struct {
  guint          refcount;
  gboolean       locked;
  gchar         *name;
  GModule       *module;
  GSList        *dependencies;
  module_info_t *info;
} loaded_module_t;

// Registry of loaded modules
// FIXME This should be a hash table
// but this needs long thinking and will not affect external interfaces
static GSList *loaded_modules = NULL;

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
  GSList        *deps = NULL;

  if (!arg || !*arg)
    return "Missing module name";

  { // Check if module is already loaded
    GSList *lmod = g_slist_find_custom(loaded_modules, arg, module_list_comparator);

    if (lmod) {
      loaded_module_t *module = lmod->data;

      if (manual) {
        if (!module->locked) {
          module->locked = TRUE;
          module->refcount += 1;
          return force ? NULL : "Module is already automatically loaded, marked as manually loaded";
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

  { // Obtain module information structure
    gchar *varname = g_strdup_printf("info_%s", arg);
    gpointer var = NULL;

    // convert to a valid symbol name
    g_strcanon(varname, "abcdefghijklmnopqrstuvwxyz0123456789", '_');

    if (!g_module_symbol(mod, varname, &var)) {
      if (!force) {
        g_free(varname);
        return "Module provides no information structure";
      }

      scr_LogPrint(LPRINT_LOGNORM, "Forced to ignore error: Module provides no information structure.");
    }

    g_free(varname);
    info = var;
  }

  // Version check
  if (info && info->mcabber_version && *(info->mcabber_version)
      && (strcmp(info->mcabber_version, PACKAGE_VERSION) > 0)) {
    if (!force) {
      g_module_close(mod);
      return "Module requires newer version of mcabber";
    }

    scr_LogPrint(LPRINT_LOGNORM, "Forced to ignore error: Module requires newer version of mcabber.");
  }

  // Load dependencies
  if (info && info->requires) {
    const gchar **dep;

    for (dep = info->requires; *dep; ++dep) {
      const gchar *err = module_load(*dep, FALSE, FALSE);

      if (err) {
        GSList *mel;
        scr_LogPrint(LPRINT_LOGNORM, "Error loading dependency module %s: %s.", *dep, err);

        // Unload already loaded dependencies
        for (mel = deps; mel; mel = mel->next) {
          gchar *ldmname = mel->data;
          err = module_unload(ldmname, FALSE, FALSE);
          scr_LogPrint(LPRINT_LOGNORM, "Error unloading dependency module %s: %s.", ldmname, err);
          g_free(ldmname);
        }
        g_slist_free(deps);

        // Unload module
        if (!g_module_close(mod))
          scr_LogPrint(LPRINT_LOGNORM, "Error unloading module %s: %s.", arg, g_module_error());
        return "Dependency problems";
      }

      deps = g_slist_append(deps, g_strdup(*dep));
    }
  }

  { // Register module
    loaded_module_t *module = g_new(loaded_module_t, 1);

    module->refcount     = 1;
    module->locked       = manual;
    module->name         = g_strdup(arg);
    module->module       = mod;
    module->info         = info;
    module->dependencies = deps;

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
        scr_LogPrint(LPRINT_LOGNORM, "Forced to ignore error: Manually unloading automatically loaded module.");
      else
        return "Module is not loaded manually";
    }
    module->locked = FALSE;
  }

  // Check refcount
  module->refcount -= 1;
  if (module->refcount > 0) {
    if (force)
      scr_LogPrint(LPRINT_LOGNORM, "Forced to ignore error: Refcount is not zero (%u).", module->refcount);
    else
      return manual ? "Module is required by some other modules" : NULL;
  }

  info = module->info;

  // Run uninitialization routine
  if (info && info->uninit)
    info->uninit();
  // XXX Prevent uninitialization routine to be called again
  module->info = NULL;

  // Unload module
  if (!g_module_close(module->module))
    return g_module_error(); // XXX destroy structure?

  { // Unload dependencies
    GSList *dep;
    for (dep = module->dependencies; dep; dep = dep->next) {
      gchar *ldmname = dep->data;
      const gchar *err = module_unload(ldmname, FALSE, FALSE);
      if (err) // XXX
        scr_LogPrint(LPRINT_LOGNORM, "Error unloading automatically loaded module %s: %s.", ldmname, err);
      g_free(ldmname);
    }
    g_slist_free(module->dependencies);
    module->dependencies = NULL;
  }

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

  // Counnt maximum module name length
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
    GSList *dep;

    g_string_append_printf(message, format, module->name, module->refcount, module->locked ? 'M' : 'A');

    // Append loaded module dependencies
    if (module->dependencies) {
      g_string_append(message, " depends: ");

      for (dep = module->dependencies; dep; dep = dep->next) {
        const gchar *name = dep->data;
        g_string_append(message, name);
        g_string_append(message, ", ");
      }

      // Chop extra ", "
      g_string_truncate(message, message->len - 2);
    }

    g_string_append_c(message, '\n');
  }

  // Chop extra "\n"
  g_string_truncate(message, message->len - 1);

  scr_LogPrint(LPRINT_LOGNORM, "%s", message->str);

  g_string_free(message, TRUE);
  g_free(format);
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

/* vim: set expandtab cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
