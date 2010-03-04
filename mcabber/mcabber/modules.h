#ifndef __MCABBER_MODULES_H__
#define __MCABBER_MODULES_H__ 1

#include <glib.h>

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

// public module-describing structure
typedef struct {
  const gchar      *mcabber_version;  // Contains mcabber version string, that this module is written to work with
  module_init_t     init;             // Initialization callback to be called after all dependencies will be loaded
  module_uninit_t   uninit;           // Uninitialization callback to be called before module unloading
  const gchar     **requires;         // NULL-terminated list of module names, that must be loaded before this module
} module_info_t;

const gchar *module_load(const gchar *name, gboolean manual, gboolean force);
const gchar *module_unload(const gchar *name, gboolean manual, gboolean force);

void module_list_print(void);

void modules_init(void);
void modules_deinit(void);

#endif
