#ifndef __MCABBER_MODULES_H__
#define __MCABBER_MODULES_H__ 1

#include <glib.h>
#include <gmodule.h>
#include <mcabber/config.h> // MCABBER_BRANCH, MCABBER_API_VERSION

// Module loading process looks like this:
//   check, if module is loaded
//   load module (+ run g_module_check_init)
//   check <modulename>_info variable
//   check version
//   load dependencies
//   run initialization callback
//   module loaded
//   ...
//   run uninitialization callback
//   unload module (+ run g_module_unload)
//   unload dependencies
//   module unloaded

typedef void (*module_init_t)(void);
typedef void (*module_uninit_t)(void);

// Structure, that module should provide
typedef struct module_info_struct module_info_t;
struct module_info_struct {
  const gchar      *branch;           // Contains mcabber branch name, that this module is written to work with
  module_init_t     init;             // Initialization callback to be called after all dependencies will be loaded
  module_uninit_t   uninit;           // Uninitialization callback to be called before module unloading
  const gchar     **requires;         // NULL-terminated list of module names, that must be loaded before this module
  guint             api;              // Mcabber branch api version, that module is supposed to work with
  const gchar      *version;          // Module version string. Optional.
  const gchar      *description;      // Module description. Can contain multiple lines.
  module_info_t    *next;             // If module supports multiple branches, it can provide several branch structs.
};

const gchar *module_load(const gchar *name, gboolean manual, gboolean force);
const gchar *module_unload(const gchar *name, gboolean manual, gboolean force);

// Grey zone (these symbols are semi-private and are exposed only for compatibility modules)

// Information about loaded module
typedef struct {
  guint          refcount;      // Reference count
  gboolean       locked;        // If true, one of references is manual
  gchar         *name;          // Module name
  GModule       *module;        // Module object
  module_info_t *info;          // Module information struct. May be NULL!
} loaded_module_t;

// Registry of loaded modules
extern GSList *loaded_modules;
extern const gchar *mcabber_branch;
extern const guint mcabber_api_version;

// Should be considered mcabber private and not a part of api

void module_list_print(void);
void module_info_print(const gchar *name);

void modules_init(void);
void modules_deinit(void);

#endif
