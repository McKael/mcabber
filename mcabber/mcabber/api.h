#ifndef __MCABBER_API_H__
#define __MCABBER_API_H__ 1

#include <glib.h>
#include <mcabber/config.h> // For MCABBER_BRANCH

#define MCABBER_API_VERSION 23
#define MCABBER_API_MIN     21

#define MCABBER_BRANCH_DEV  1

#define MCABBER_API_HAVE_CMD_ID 1

extern const gchar *mcabber_branch;
extern const guint mcabber_api_version;

#endif
/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
