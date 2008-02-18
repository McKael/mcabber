#ifndef __FIFO_H__
#define __FIFO_H__ 1

int  fifo_init(const char *fifo_path);
void fifo_deinit(void);
void fifo_read(void);
int  fifo_get_fd(void);

#endif /* __FIFO_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
