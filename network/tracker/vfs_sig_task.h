#ifndef __VFS_SIG_TASK_H_
#define __VFS_SIG_TASK_H_
#include "list.h"
#include "global.h"
#include "common.h"
#include "protocol.h"
#include "vfs_init.h"
#include "vfs_task.h"
#include "vfs_sig.h"
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

#define TMP_TASKINFO_SIZE 300

int do_newtask(int fd, t_vfs_sig_head *h, t_vfs_sig_body *b);

int update_task(int fd, t_vfs_sig_head *h, t_vfs_sig_body *b);

int do_dispatch(t_vfs_tasklist *task);

typedef struct {
	list_head_t list;
}t_tracker_task_info;

#endif
