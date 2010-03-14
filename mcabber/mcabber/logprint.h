#ifndef __MCABBER_LOGPRINT_H__
#define __MCABBER_LOGPRINT_H__ 1

#include <glib.h>

// Flags for scr_LogPrint()
#define LPRINT_NORMAL   1U  // Display in log window
#define LPRINT_LOG      2U  // Log to file (if enabled)
#define LPRINT_DEBUG    4U  // Debug message (log if enabled)
#define LPRINT_NOTUTF8  8U  // Do not convert from UTF-8 to locale

// For convenience...
#define LPRINT_LOGNORM  (LPRINT_NORMAL|LPRINT_LOG)

void scr_print_logwindow(const char *string);
void scr_log_print(unsigned int flag, const char *fmt, ...) G_GNUC_PRINTF (2, 3);
void scr_do_update(void);

// For backward compatibility:
#define scr_LogPrint    scr_log_print

#endif /* __MCABBER_LOGPRINT_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
