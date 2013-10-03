#include "vfs_sig.h"
#include "vfs_sig_task.h"
#include "common.h"
#include "global.h"
#include "vfs_so.h"
#include "myepoll.h"
#include "protocol.h"
#include "vfs_sig.h"
#include "vfs_tmp_status.h"
#include "util.h"
#include "acl.h"

extern int vfs_sig_log;
extern int vfs_sig_log_err;

void do_rsp(vfs_tracker_peer *peer, t_task_base *base)
{
	char obuf[2048] = {0x0};
	int fd = peer->fd;
	int n = create_sig_msg(NEWTASK_RSP, base->overstatus, (t_vfs_sig_body*)base, obuf, sizeof(t_task_base));
	set_client_data(fd, obuf, n);
	peer->sock_stat = SEND_LAST;
	modify_fd_event(fd, EPOLLOUT);
	LOG(vfs_sig_log, LOG_DEBUG, "%s:%s:%d:%s\n", ID, FUNC, LN, base->filename);
}

void dump_task_info (char *from, int line, t_task_base *task)
{
	char ip[16] = {0x0};
	if (task->dstip)
	{
		ip2str(ip, task->dstip);
		LOG(vfs_sig_log, LOG_DEBUG, "from %s:%d filename [%s] srcdomain [%s] filesize[%ld] filemd5 [%s] filectime [%ld] type [%c] dstip[%s]\n", from, line, task->filename, task->src_domain, task->fsize, task->filemd5, task->ctime, task->type, ip);
	}
	else
		LOG(vfs_sig_log, LOG_DEBUG, "from %s:%d filename [%s] srcdomain [%s] filesize[%ld] filemd5 [%s] filectime [%ld] type [%c] dstip is null\n", from, line, task->filename, task->src_domain, task->fsize, task->filemd5, task->ctime, task->type);
}

static void process_timeout (t_vfs_tasklist *task)
{
	t_task_base * base = &(task->task.base);
	LOG(vfs_sig_log_err, LOG_ERROR, "start process timeout %s:%s:%c\n", base->src_domain, base->filename, base->type);
	if (base->overstatus == OVER_UNKNOWN)
		base->overstatus = OVER_TIMEOUT;
	dump_task_info ((char *) FUNC, LN, base);
	list_del_init(&(task->userlist));
	vfs_set_task(task, TASK_CLEAN);
}

static uint32_t get_a_good_ip(t_cs_dir_info * csinfos, int isp, t_vfs_taskinfo *task)
{
	if (csinfos->index == 0)
	{
		LOG(vfs_sig_log_err, LOG_ERROR, "cs_dir index %d\n", csinfos->index);
		return 0;
	}
	uint32_t ip = 0;
	int i= 0;
	int mintask = 0;
	t_ip_info ipinfo0;
	t_ip_info *ipinfo = &ipinfo0;
	vfs_tracker_peer *peer;
	for (i = 0; i < csinfos->index; i++)
	{
		if (csinfos->isp[i] != isp)
			continue;
		if (find_ip_stat(csinfos->ip[i], &peer))
		{
			LOG(vfs_sig_log_err, LOG_ERROR, "ip [%u] not active !\n", csinfos->ip[i]);
			continue;
		}
		if (get_ip_info_by_uint(&ipinfo, csinfos->ip[i], 1, " ", " "))
		{
			LOG(vfs_sig_log_err, LOG_ERROR, "dispatch get_ip_info %u err %m\n", csinfos->ip[i]);
			continue;
		}
		if (ipinfo->offline)
		{
			LOG(vfs_sig_log_err, LOG_ERROR, "dispatch %u offline\n", csinfos->ip[i]);
			continue;
		}
		if (ip == 0)
		{
			ip = csinfos->ip[i];
			mintask = peer->taskcount;
			continue;
		}
		if (peer->taskcount <= mintask)
		{
			ip = csinfos->ip[i];
			mintask = peer->taskcount;
		}
	}
	return ip;
}

static void create_delay_task(int isp, t_task_base *base)
{
	t_vfs_tasklist *task = NULL;
	if (vfs_get_task(&task, TASK_HOME))
	{
		LOG(vfs_sig_log_err, LOG_ERROR, "[%s] do_newtask ERROR!\n", FUNC);
		return;
	}
	list_del_init(&(task->userlist));
	base->starttime = time(NULL);
	memset(&(task->task), 0, sizeof(task->task));
	memcpy(&(task->task.base), base, sizeof(t_task_base));

	dump_task_info ((char *) FUNC, LN, base);
	task->task.sub.isp = isp;
	vfs_set_task(task, TASK_WAIT);
	LOG(vfs_sig_log, LOG_DEBUG, "[%s] [%d] create_delay_task!\n", base->filename, isp);
	set_task_to_tmp(task);
}

int do_dispatch(t_vfs_tasklist *task)
{
	t_task_base *base = (t_task_base*) &(task->task.base);
	t_cs_dir_info  cs;
	if(get_cs_info_by_path(base->filename, &cs))
	{
		LOG(vfs_sig_log_err, LOG_ERROR, "get_cs_info_by_path %s ERROR!\n", base->filename);
		return 1;
	}
	uint32_t ip = get_a_good_ip(&cs, task->task.sub.isp, &(task->task));
	if (ip == 0)
	{
		LOG(vfs_sig_log_err, LOG_ERROR, "can not get good ip isp:%s:%s\n", ispname[task->task.sub.isp], base->filename);
		return 1;
	}
	vfs_tracker_peer *peer;
	if (find_ip_stat(ip, &peer))
	{
		LOG(vfs_sig_log_err, LOG_ERROR, "find_ip_stat [%u] ERROR [%s:%s:%d]!\n", ip, ID, FUNC, LN);
		return 1;
	}

	LOG(vfs_sig_log, LOG_DEBUG, "%s select %u as source\n", base->filename, ip);

	base->dstip = ip;
	char obuf[2048] = {0x0};
	size_t n = 0;
	peer->hbtime = time(NULL);
	n = create_sig_msg(NEWTASK_REQ, TASK_START, (t_vfs_sig_body *)base, obuf, sizeof(t_task_base));
	set_client_data(peer->fd, obuf, n);
	modify_fd_event(peer->fd, EPOLLOUT);
	list_add(&(task->userlist), &(peer->tasklist));
	peer->taskcount++;
	return 0;
}

int do_newtask(int fd, t_vfs_sig_head *h, t_vfs_sig_body *b)
{
	t_task_base * base = (t_task_base *)b;
	base->starttime = time(NULL);
	create_delay_task(TEL, base);
	create_delay_task(CNC, base);
	return 0;
}
  
int get_rsp_peer(vfs_tracker_peer **peer, t_task_base * base)
{
	uint32_t ip = 0;
	t_ip_info ipinfo0;
	t_ip_info *ipinfo = &ipinfo0;
	if (get_ip_info(&ipinfo, base->src_domain, 1))
	{
		LOG(vfs_sig_log_err, LOG_ERROR, "task %s domain[%s] error ip!\n", base->filename,  base->src_domain);
		base->overstatus = OVER_SRC_DOMAIN_ERR;
		return 1;
	}
	else
	{
		ip = ipinfo->ip;
		if (find_ip_stat(ip, peer))
		{
			LOG(vfs_sig_log_err, LOG_ERROR, "task %s domain[%s:%s]offline!\n", base->filename, ipinfo->s_ip, base->src_domain);
			base->overstatus = OVER_SRC_IP_OFFLINE;
			return 1;
		}
	}
	return 0;
}

int update_task(int fd, t_vfs_sig_head *h, t_vfs_sig_body *b)
{
	struct conn *curcon = &acon[fd];
	vfs_tracker_peer *peer_cs = (vfs_tracker_peer *) curcon->user;
	t_task_base * base = (t_task_base *)b;
	t_task_sub sub;
	memset(&sub, 0, sizeof(sub));
	sub.isp = peer_cs->isp;
	t_vfs_tasklist *task = NULL;
	vfs_tracker_peer *peer = NULL;

	if (get_task_from_alltask(&task, base, &sub))
	{
		LOG(vfs_sig_log_err, LOG_ERROR, "fd[%d] task %s not exist!\n", fd, base->filename);
		if(get_rsp_peer(&peer, base) == 0)
			do_rsp(peer, base);
		return -1;
	}
	if (task->task.user)
	{
		t_tmp_status *tmp = task->task.user;
		set_tmp_blank(tmp->pos, tmp);
		task->task.user = NULL;
	}
	peer_cs->taskcount--;
	if (peer_cs->taskcount < 0)
		peer_cs->taskcount = 0;
	task->task.base.overstatus = base->overstatus;
	LOG(vfs_sig_log, LOG_DEBUG, "task %s status [%x]!\n", base->filename, base->overstatus);

	if(get_rsp_peer(&peer, base) == 0)
		do_rsp(peer, base);
	else
	{
		process_timeout(task);
		return 0;
	}

	list_del_init(&(task->userlist));
	vfs_set_task(task, TASK_CLEAN);
	return 0;
}


