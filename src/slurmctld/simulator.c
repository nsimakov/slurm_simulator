#ifdef SLURM_SIMULATOR


#include "config.h"

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <errno.h>
#include <grp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/checkpoint.h"
#include "src/common/daemonize.h"
#include "src/common/fd.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/layouts_mgr.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/power.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_priority.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_route.h"
#include "src/common/slurm_topology.h"
#include "src/common/switch.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/job_submit.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"


//fill slurm_node_registration_status_msg_t from fake slurmd
static void _sim_fill_slurm_node_registration_status_msg(slurm_node_registration_status_msg_t *msg)
{
	char *arch, *os;
	struct utsname buf;
	static bool first_msg = true;
	Buf gres_info;
	//slurmd_conf_t * conf = conf = xmalloc(sizeof(slurmd_conf_t));
	//_init_conf();
	slurm_ctl_conf_t *conf=&slurmctld_conf;

	msg->node_name   = xstrdup (conf->control_addr);//in sim same
	msg->version     = xstrdup (PACKAGE_VERSION);

	//this is slurmd machine not node, so doe not matter
	msg->cpus	 = 4;
	msg->boards	 = 1;
	msg->sockets	 = 1;
	msg->cores	 = 2;
	msg->threads	 = 2;
	//if (res_abs_cpus[0] == '\0')
		msg->cpu_spec_list = NULL;
	//else
	//	msg->cpu_spec_list = xstrdup (res_abs_cpus);
	msg->real_memory = 8192;
	msg->tmp_disk    = 8192;
	msg->hash_val    = slurm_get_hash_val();
	msg->cpu_load=0;
	msg->free_mem=4096;

	gres_info = init_buf(1024);
	if (gres_plugin_node_config_pack(gres_info) != SLURM_SUCCESS)
		error("error packing gres configuration");
	else
		msg->gres_info   = gres_info;

	msg->up_time     = 300;
	msg->slurmd_start_time = time(NULL)-msg->up_time;

	if (first_msg) {
		first_msg = false;
		info("CPUs=%u Boards=%u Sockets=%u Cores=%u Threads=%u "
		     "Memory=%lu TmpDisk=%u Uptime=%u CPUSpecList=%s",
		     msg->cpus, msg->boards, msg->sockets, msg->cores,
		     msg->threads, msg->real_memory, msg->tmp_disk,
		     msg->up_time, msg->cpu_spec_list);
	} else {
		debug3("CPUs=%u Boards=%u Sockets=%u Cores=%u Threads=%u "
		       "Memory=%lu TmpDisk=%u Uptime=%u CPUSpecList=%s",
		       msg->cpus, msg->boards, msg->sockets, msg->cores,
		       msg->threads, msg->real_memory, msg->tmp_disk,
		       msg->up_time, msg->cpu_spec_list);
	}

	uname(&buf);
	if ((arch = getenv("SLURM_ARCH")))
		msg->arch = xstrdup(arch);
	else
		msg->arch = xstrdup(buf.machine);
	if ((os = getenv("SLURM_OS")))
		msg->os   = xstrdup(os);
	else
		msg->os = xstrdup(buf.sysname);

	if (msg->startup) {
		if (switch_g_alloc_node_info(&msg->switch_nodeinfo))
			error("switch_g_alloc_node_info: %m");
		if (switch_g_build_node_info(msg->switch_nodeinfo))
			error("switch_g_build_node_info: %m");
	}

	msg->job_count = 0;
	msg->job_id    = NULL;
	/* Note: Running batch jobs will have step_id == NO_VAL */
	msg->step_id   = NULL;

	if (!msg->energy)
		msg->energy = acct_gather_energy_alloc(1);
	acct_gather_energy_g_get_data(ENERGY_DATA_NODE_ENERGY, msg->energy);

	msg->timestamp = time(NULL);

	return;
}

//fake a call from frontend slurmd and register nodes
static int _sim_register_nodes()
{
	int error_code = SLURM_SUCCESS;

	bool newly_up = false;

	slurm_node_registration_status_msg_t *node_regmsg =
		xmalloc (sizeof (slurm_node_registration_status_msg_t));

	node_regmsg->startup = (uint16_t) 1;
	_sim_fill_slurm_node_registration_status_msg(node_regmsg);
	node_regmsg->status  = SLURM_SUCCESS;

	error_code = validate_nodes_via_front_end(node_regmsg,
			SLURM_PROTOCOL_VERSION, &newly_up);

	slurm_free_node_registration_status_msg(node_regmsg);

	return error_code;
}

extern void sim_controller()
{
	//register nodes
	_sim_register_nodes();
	//simulation controller main loop
	while(1)
	{
		info("SIM main loop\n");
		sleep(1);
	}
}

#endif
