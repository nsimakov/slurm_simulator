#include "config.h"

#ifdef SLURM_SIMULATOR
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
#include <sys/times.h>

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

#include "src/common/sim/sim.h"


/* reference to priority multifactor decay */
int (*_sim_run_priority_decay)(void)=NULL;

extern int sched_interval;
static int      sim_job_sched_cnt = 0;
static pthread_mutex_t sim_sched_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;

void sim_sdiag_mini();

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
		     "Memory=%u TmpDisk=%u Uptime=%u CPUSpecList=%s",
		     msg->cpus, msg->boards, msg->sockets, msg->cores,
		     msg->threads, msg->real_memory, msg->tmp_disk,
		     msg->up_time, msg->cpu_spec_list);
	} else {
		debug3("CPUs=%u Boards=%u Sockets=%u Cores=%u Threads=%u "
		       "Memory=%u TmpDisk=%u Uptime=%u CPUSpecList=%s",
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
static int _sim_register_nodes_from_frontend_slurmd()
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

/* kill threads which are not impotent for simulation */
static int _sim_kill_not_critical_threads()
{
	int error_code = SLURM_SUCCESS;
	//pthread_t tid_self=pthread_self();
	//slurmctld_config.server_thread_count;
	//slurmctld_config.thread_id_rpc;
	//pthread_cancel
	return error_code;
}


static void print_job_specs(job_desc_msg_t* dmesg)
{
	debug("\tdmesg->job_id: %d\n", dmesg->job_id);
	debug2("\t\tdmesg->time_limit: %d\n", dmesg->time_limit);
	debug2("\t\tdmesg->name: (%s)\n", dmesg->name);
	debug2("\t\tdmesg->user_id: %d\n", dmesg->user_id);
	debug2("\t\tdmesg->group_id: %d\n", dmesg->group_id);
	debug2("\t\tdmesg->work_dir: (%s)\n", dmesg->work_dir);
	debug2("\t\tdmesg->qos: (%s)\n", dmesg->qos);
	debug2("\t\tdmesg->partition: (%s)\n", dmesg->partition);
	debug2("\t\tdmesg->account: (%s)\n", dmesg->account);
	debug2("\t\tdmesg->reservation: (%s)\n", dmesg->reservation);
	debug2("\t\tdmesg->dependency: (%s)\n", dmesg->dependency);
	debug2("\t\tdmesg->num_tasks: %d\n", dmesg->num_tasks);
	debug2("\t\tdmesg->min_cpus: %d\n", dmesg->min_cpus);
	debug2("\t\tdmesg->cpus_per_task: %d\n", dmesg->cpus_per_task);
	debug2("\t\tdmesg->ntasks_per_node: %d\n", dmesg->ntasks_per_node);
	//debug2("\t\tdmesg->duration: %d\n", dmesg->duration);
	debug2("\t\tdmesg->env_size: %d\n", dmesg->env_size);
	debug2("\t\tdmesg->environment[0]: (%s)\n", dmesg->environment[0]);
	debug2("\t\tdmesg->script: (%s)\n", dmesg->script);
}

/* simulator wrapper around _slurm_rpc_submit_batch_job - process RPC to submit a batch job */
static int _sim_slurm_rpc_submit_batch_job(slurm_msg_t * msg)
{
	int error_code = SLURM_SUCCESS;

	//_slurm_rpc_submit_batch_job(msg);

	char *err_msg = NULL;
	struct job_record *job_ptr = NULL;
	uid_t uid = g_slurm_auth_get_uid(msg->auth_cred,
						 slurmctld_config.auth_info);

	job_desc_msg_t *job_desc_msg = (job_desc_msg_t *) msg->data;

	error_code = job_allocate(job_desc_msg,
							  job_desc_msg->immediate,
							  false, NULL, 0, uid, &job_ptr,
							  &err_msg,
							  msg->protocol_version);
	if(error_code!=SLURM_SUCCESS){
		info("_slurm_rpc_submit_batch_job: %s\n\t%s",
			     slurm_strerror(error_code),err_msg);
	}
	xfree(err_msg);
	return error_code;
}

static int _sim_submit_job(job_trace_t* jobd)
{
	job_desc_msg_t dmesg;
	submit_response_msg_t * respMsg = NULL;
	int rv = 0;
	char *script, line[1024];
	//uid_t uidt;
	//gid_t gidt;

	script = xstrdup("#!/bin/bash\n");

	slurm_init_job_desc_msg(&dmesg);

	/* Set up and call Slurm C-API for actual job submission. */
	dmesg.time_limit    = jobd->wclimit;
	dmesg.job_id        = jobd->job_id;
	sprintf(line,"%d", jobd->job_id);
	dmesg.name	    = xstrdup(line);
	sim_user_info_t *user_info=get_sim_user_by_name(jobd->username);
	if(user_info==NULL){
		info("Error:SIM: unknown user for simulator: %s",jobd->username);
		return SLURM_FAILURE;
	}
	dmesg.user_id       = user_info->sim_uid;
	dmesg.group_id      = user_info->sim_gid;
	dmesg.work_dir      = xstrdup("/tmp"); /* hardcoded to /tmp for now */
	dmesg.qos           = xstrdup(jobd->qosname);
	dmesg.partition     = xstrdup(jobd->partition);
	dmesg.account       = xstrdup(jobd->account);
	dmesg.reservation   = xstrdup(jobd->reservation);
	dmesg.dependency    = xstrdup(jobd->dependency);
	dmesg.num_tasks     = jobd->tasks;
	dmesg.min_cpus      = jobd->tasks;
	dmesg.cpus_per_task = jobd->cpus_per_task;
	dmesg.ntasks_per_node = jobd->tasks_per_node;
	//dmesg.duration      = jobd->duration;

	dmesg.pn_min_memory = jobd->pn_min_memory;
	dmesg.features=xstrdup(jobd->features);
	dmesg.gres=xstrdup(jobd->gres);
	dmesg.shared=jobd->shared;

	/* Need something for environment--Should make this more generic! */
	dmesg.environment  = (char**)xmalloc(sizeof(char*)*2);
	dmesg.environment[0] = xstrdup("HOME=/root");
	dmesg.env_size = 1;

	/* Standard dummy script. */
	sprintf(line,"#SBATCH -n %u\n", jobd->tasks);
	xstrcat(script, line);
	xstrcat(script, "\necho \"Generated BATCH Job\"\necho \"La Fine!\"\n");

	dmesg.script        = xstrdup(script);

	print_job_specs(&dmesg);

	/*if ( slurm_submit_batch_job(&dmesg, &respMsg) ) {
		slurm_perror ("slurm_submit_batch_job");
		rv = -1;
	}*/

	job_desc_msg_t *req=&dmesg;
    slurm_msg_t req_msg;
    //slurm_msg_t resp_msg;
	//bool host_set = false;
	char host[64];

	slurm_msg_t_init(&req_msg);
	//slurm_msg_t_init(&resp_msg);


	/*
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	if ( (req->alloc_node == NULL)
		&& (gethostname_short(host, sizeof(host)) == 0) ) {
		req->alloc_node = host;
		//host_set  = true;
	}

	req_msg.msg_type = REQUEST_SUBMIT_BATCH_JOB ;
	req_msg.data     = req;

	req_msg.auth_cred=g_slurm_auth_create(slurm_get_auth_info());

	int error_code=_sim_slurm_rpc_submit_batch_job(&req_msg);


	//if (respMsg)
	if(error_code==0)
		info("Successfully submit job to queue (job_id: %u)", dmesg.job_id);
	else
		info("Failed submission of job to queue, error_code: %u job_id: %u",
				error_code, dmesg.job_id);

	/* add to cancel event if job needed to be cancelled */
	if(jobd->cancelled>0)
		sim_add_future_cancel_event(jobd);

	/* Cleanup */
	if (respMsg) slurm_free_submit_response_response_msg(respMsg);
	if (script) xfree(script);
	if (dmesg.name)        xfree(dmesg.name);
	if (dmesg.work_dir)    xfree(dmesg.work_dir);
	if (dmesg.qos)         xfree(dmesg.qos);
	if (dmesg.partition)   xfree(dmesg.partition);
	if (dmesg.account)     xfree(dmesg.account);
	if (dmesg.reservation) xfree(dmesg.reservation);
	if (dmesg.dependency)  xfree(dmesg.dependency);
	if (dmesg.script)      xfree(dmesg.script);
	xfree(dmesg.environment[0]);
	xfree(dmesg.environment);

	return rv;
}

/*
 * this function is called from job_scheduler.c::launch_job(job_ptr)
 * instead of spin-off agent_queue_request
 */
extern void sim_agent_queue_request_joblaunch(struct job_record *job_ptr, batch_job_launch_msg_t *launch_msg_ptr)
{
	info("SIM: faking sending message type REQUEST_BATCH_JOB_LAUNCH "
						"to %s\n", job_ptr->batch_host);

	sim_add_future_event(launch_msg_ptr);


	if(slurm_sim_conf->after_job_launch_time_increament>0){
		sim_pause_clock();
		sim_incr_clock(slurm_sim_conf->after_job_launch_time_increament);
		sim_resume_clock();
	}


	slurm_free_job_launch_msg(launch_msg_ptr);
}

static int _sim_send_complete_batch_script_msg(uint32_t jobid, int err, int status)
{
	//_slurm_rpc_complete_batch_script(&req_msg, 0);
	uid_t uid = g_slurm_auth_get_uid(g_slurm_auth_create(slurm_get_auth_info()), slurmctld_config.auth_info);

	int error_code = job_complete(jobid, uid, false, false, SLURM_SUCCESS);
	if(error_code!=SLURM_SUCCESS){
		info("ERROR:SIM: can not job_complete for job %d",jobid);
	}
	return SLURM_SUCCESS;
}
/*
 * return number of processed jobs
 */
extern int sim_process_finished_jobs()
{
	int jobs_ended=0;
	time_t now;
	//bool run_scheduler;

	now = time(NULL);
	while ((head_simulator_event) &&
			(now >= head_simulator_event->when)) {

		simulator_event_t *aux;
		int event_jid;

		event_jid               = head_simulator_event->job_id;
		aux                     = head_simulator_event;
		head_simulator_event    = head_simulator_event->next;
		//aux->next               = head_sim_completed_jobs;
		//head_sim_completed_jobs = aux;
		//_simulator_remove_job_from_nodes(head_sim_completed_jobs);

		//--total_sim_events;
		(*sim_jobs_done)++;

		//struct job_record *job_ptr = find_job_record(event_jid);


		debug2("SIM: Sending JOB_COMPLETE_BATCH_SCRIPT"
			" for job %d", event_jid);

		//pthread_mutex_unlock(&simulator_mutex);
		_sim_send_complete_batch_script_msg(event_jid,
			SLURM_SUCCESS, 0);
		//pthread_mutex_lock(&simulator_mutex);

		slurm_msg_t            msg;
		epilog_complete_msg_t  req;

		slurm_msg_t_init(&msg);


		req.job_id      = event_jid;
		req.return_code = SLURM_SUCCESS;
		req.node_name   = "localhost";

		msg.msg_type    = MESSAGE_EPILOG_COMPLETE;
		msg.data        = &req;

		//_slurm_rpc_epilog_complete(&msg, (bool *)&run_scheduler, 0);
		if(job_epilog_complete(req.job_id, req.node_name,req.return_code)){

		}else{
			info("ERROR:SIM: can not job_epilog_complete for job %d",event_jid);
		}

		debug2("SIM: JOB_COMPLETE_BATCH_SCRIPT for "
			"job %d SENT", event_jid);

		remove_from_in_queue_trace_record(find_job__in_queue_trace_record(event_jid));

		++jobs_ended;
		sim_job_sched_cnt++;

	}
	return jobs_ended;
}
/*
 * return number of cancelled jobs
 */
extern int sim_cancel_jobs()
{
	int jobs_cancelled=0;
	time_t now;
	//bool run_scheduler;

	now = time(NULL);
	while ((head_simulator_cancel_event) &&
			(now >= head_simulator_cancel_event->when)) {

		simulator_event_t *aux;
		int event_jid;

		event_jid               = head_simulator_cancel_event->job_id;
		aux                     = head_simulator_cancel_event;
		head_simulator_cancel_event    = head_simulator_cancel_event->next;


		struct job_record *job_ptr = find_job_record(event_jid);
		job_trace_t* trace = find_job__in_queue_trace_record(event_jid);

		if(job_ptr==NULL && trace!=NULL)
		{
			error("trying to cancell job %d but it is not in queue but still in in_queue_trace_record",event_jid);
			remove_from_in_queue_trace_record(trace);
		}
		//if job is already running it will be cancelled by sim_process_finished_jobs
		if(job_ptr!=NULL && !IS_JOB_RUNNING(job_ptr))
		{
			(*sim_jobs_done)++;
			debug2("SIM: Sending REQUEST_CANCEL_JOB for job %d", event_jid);
			uid_t uid = g_slurm_auth_get_uid(g_slurm_auth_create(slurm_get_auth_info()), slurmctld_config.auth_info);
			job_signal(event_jid, SIGKILL, 0, uid,false);

			remove_from_in_queue_trace_record(trace);

			++jobs_cancelled;
			sim_job_sched_cnt++;
		}
	}
	return jobs_cancelled;
}

/* reference to priority multifactor decay */
void sim_run_priority_decay(){
	time_t cur_time;
	static time_t next_run_priority_decay = 0;

	if(_sim_run_priority_decay!=NULL){
		if(next_run_priority_decay<time(NULL)){
			(*_sim_run_priority_decay)();
			next_run_priority_decay=time(NULL)+slurmctld_conf.priority_calc_period;
		}
	}
}
/* execure scheduler from schedule_plugin */
extern void schedule_plugin_run_once()
{
	int backfill_was_ran=0;

	double t=get_realtime();
	struct tms m_tms0,m_tms1;

	clock_t st=clock();
	times(&m_tms0);
	if (sim_backfill_agent_ref!=NULL){
		backfill_was_ran=(*sim_backfill_agent_ref)();
	} else {
		info("Error: sched_plugin do not support simulator");
	}
	times(&m_tms1);
	st=clock()-st;
	t=get_realtime()-t;

	clock_t tms_utime=m_tms1.tms_utime-m_tms0.tms_utime;
	clock_t tms_stime=m_tms1.tms_stime-m_tms0.tms_stime;

	if(backfill_was_ran && slurm_sim_conf->sim_stat!=NULL){
		int jobs_pending=0;
		int jobs_running=0;

		int nodes_idle=0;
		int nodes_mixed=0;
		int nodes_allocated=0;

		if(slurm_sim_conf->sim_stat!=NULL){
			ListIterator job_iterator;
			struct job_record *job_ptr;


			job_iterator = list_iterator_create(job_list);
			while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
				if(IS_JOB_PENDING(job_ptr))jobs_pending++;
				if(IS_JOB_RUNNING(job_ptr))jobs_running++;
			}
			list_iterator_destroy(job_iterator);

			int inx;
			struct node_record *node_ptr = node_record_table_ptr;
			for (inx = 0; inx < node_record_count; inx++, node_ptr++) {
				if(IS_NODE_IDLE(node_ptr))nodes_idle++;
				if(IS_NODE_MIXED(node_ptr))nodes_mixed++;
				if(IS_NODE_ALLOCATED(node_ptr))nodes_allocated++;
			}
		}


		FILE *fout=fopen(slurm_sim_conf->sim_stat,"at");
		if(fout==NULL)
			return;

		time_t now = time(NULL);

		fprintf(fout, "*Backfill*Stats****************************************\n");
		fprintf(fout, "Output time: %s", ctime(&now));
		fprintf(fout, "\tLast cycle when: %s", ctime(&slurmctld_diag_stats.bf_when_last_cycle));
		fprintf(fout, "\tLast cycle: %u\n", slurmctld_diag_stats.bf_cycle_last);
		fprintf(fout, "\tLast depth cycle: %u\n", slurmctld_diag_stats.bf_last_depth);
		fprintf(fout, "\tLast depth cycle (try sched): %u\n", slurmctld_diag_stats.bf_last_depth_try);
		fprintf(fout, "\tLast queue length: %u\n", slurmctld_diag_stats.bf_queue_len);
		fprintf(fout, "\tLast backfilled jobs: %u\n", slurmctld_diag_stats.last_backfilled_jobs);

		fprintf(fout, "\tRun real time: %.6f\n", t);
		fprintf(fout, "\tRun real utime: %ld\n",tms_utime);
		fprintf(fout, "\tRun real stime: %ld\n",tms_stime);
		fprintf(fout, "\tCLK_TCK: %ld\n", sysconf (_SC_CLK_TCK));
		fprintf(fout, "\tRun clock: %ld\n",st);
		fprintf(fout, "\tCLOCKS_PER_SEC: %ld\n",CLOCKS_PER_SEC);

		fprintf(fout, "\tjobs_pending: %d\n",jobs_pending);
		fprintf(fout, "\tjobs_running: %d\n",jobs_running);
		fprintf(fout, "\tnodes_idle: %d\n",nodes_idle);
		fprintf(fout, "\tnodes_mixed: %d\n",nodes_mixed);
		fprintf(fout, "\tnodes_allocated: %d\n",nodes_allocated);

		fclose(fout);
	}
	if(backfill_was_ran && slurm_sim_conf->sdiag_file_out!=NULL){
		sim_sdiag_mini();
	}

}




int sim_schedule()
{
	int jobs_scheduled;
	int purge_job_interval=60;
	uint32_t job_limit;

	static time_t last_sched_time = 0;
	static time_t last_full_sched_time = 0;
	//static time_t last_checkpoint_time = 0;
	static time_t last_purge_job_time = 0;
	//static time_t last_priority_calc_period = 0;

	time_t now;
	now = time(NULL);

	if (last_sched_time == 0){
		//last_sched_time = now;
		//last_full_sched_time = now;
		//last_checkpoint_time = now;
		//last_purge_job_time = now;
	}

	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock2 = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };



	if (difftime(now, last_purge_job_time) >= purge_job_interval) {
		now = time(NULL);
		last_purge_job_time = now;
		debug2("Performing purge of old job records");
		lock_slurmctld(job_write_lock);
		purge_old_job();
		unlock_slurmctld(job_write_lock);
	}


	job_limit = NO_VAL;
	if (difftime(now, last_full_sched_time) >= sched_interval) {
		slurm_mutex_lock(&sim_sched_cnt_mutex);
		/* job_limit = job_sched_cnt;	Ignored */
		job_limit = INFINITE;
		sim_job_sched_cnt = 0;
		slurm_mutex_unlock(&sim_sched_cnt_mutex);
		last_full_sched_time = now;
	} else {
		slurm_mutex_lock(&sim_sched_cnt_mutex);
		if (sim_job_sched_cnt &&
		    (difftime(now, last_sched_time) >=
		     batch_sched_delay)) {
			job_limit = 0;	/* Default depth */
			sim_job_sched_cnt = 0;
		}
		slurm_mutex_unlock(&sim_sched_cnt_mutex);
	}
	if (job_limit != NO_VAL) {
		now = time(NULL);
		last_sched_time = now;
		lock_slurmctld(job_write_lock2);
		bb_g_load_state(false);	/* May alter job nice/prio */
		unlock_slurmctld(job_write_lock2);

		//sim_pause_clock();
		int jobs_pending=0;
		int jobs_running=0;

		int nodes_idle=0;
		int nodes_mixed=0;
		int nodes_allocated=0;


		if(slurm_sim_conf->sim_stat!=NULL){
			ListIterator job_iterator;
			struct job_record *job_ptr;


			job_iterator = list_iterator_create(job_list);
			while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
				if(IS_JOB_PENDING(job_ptr))jobs_pending++;
				if(IS_JOB_RUNNING(job_ptr))jobs_running++;
			}
			list_iterator_destroy(job_iterator);

			int inx;
			struct node_record *node_ptr = node_record_table_ptr;
			for (inx = 0; inx < node_record_count; inx++, node_ptr++) {
				if(IS_NODE_IDLE(node_ptr))nodes_idle++;
				if(IS_NODE_MIXED(node_ptr))nodes_mixed++;
				if(IS_NODE_ALLOCATED(node_ptr))nodes_allocated++;
			}
		}

		double t=get_realtime();
		struct tms m_tms0,m_tms1;
		clock_t st=clock();
		times(&m_tms0);
		jobs_scheduled=schedule(job_limit);
		times(&m_tms1);
		st=clock()-st;
		t=get_realtime()-t;

		clock_t tms_utime=m_tms1.tms_utime-m_tms0.tms_utime;
		clock_t tms_stime=m_tms1.tms_stime-m_tms0.tms_stime;

		if(slurm_sim_conf->sim_stat!=NULL&&jobs_scheduled>0){
			FILE *fout=fopen(slurm_sim_conf->sim_stat,"at");

			time_t now = time(NULL);

			fprintf(fout, "*Scheduler*Stats***************************************\n");
			fprintf(fout, "Output time: %s", ctime(&now));

			fprintf(fout, "\tLast cycle:   %u\n", slurmctld_diag_stats.schedule_cycle_last);
			fprintf(fout, "\tLast depth cycle: %u\n", slurmctld_diag_stats.schedule_cycle_depth);
			fprintf(fout, "\tLast scheduled: %u\n", jobs_scheduled);
			fprintf(fout, "\tLast queue length: %u\n", slurmctld_diag_stats.schedule_queue_len);

			fprintf(fout, "\tRun real time: %.6f\n", t);
			fprintf(fout, "\tRun real utime: %ld\n",tms_utime);
			fprintf(fout, "\tRun real stime: %ld\n",tms_stime);

			fprintf(fout, "\tCLK_TCK: %ld\n", sysconf (_SC_CLK_TCK));
			fprintf(fout, "\tRun clock: %ld\n",st);
			fprintf(fout, "\tCLOCKS_PER_SEC: %ld\n",CLOCKS_PER_SEC);

			fprintf(fout, "\tjobs_pending: %d\n",jobs_pending);
			fprintf(fout, "\tjobs_running: %d\n",jobs_running);
			fprintf(fout, "\tnodes_idle: %d\n",nodes_idle);
			fprintf(fout, "\tnodes_mixed: %d\n",nodes_mixed);
			fprintf(fout, "\tnodes_allocated: %d\n",nodes_allocated);

			fclose(fout);
		}


		//sim_resume_clock();

		set_job_elig_time();
	}

	return jobs_scheduled;
}
/*printout like from sdiag*/
void sim_sdiag_mini()
{
	if(slurm_sim_conf->sdiag_file_out==NULL)
		return;

	FILE *fout=fopen(slurm_sim_conf->sdiag_file_out,"at");
	if(fout==NULL)
		return;

	time_t now = time(NULL);

	fprintf(fout, "*******************************************************\n");
	fprintf(fout, "sdiag output time: %s", ctime(&now));
	fprintf(fout, "Data since:        %s", ctime(&last_proc_req_start));
	fprintf(fout, "*******************************************************\n");

	fprintf(fout, "Server thread count: %d\n", slurmctld_config.server_thread_count);
	fprintf(fout, "Agent queue size:    %d\n\n", retry_list_size());
	fprintf(fout, "Jobs submitted: %d\n", slurmctld_diag_stats.jobs_submitted);
	fprintf(fout, "Jobs started:   %d\n", slurmctld_diag_stats.jobs_started);
	fprintf(fout, "Jobs completed: %d\n", slurmctld_diag_stats.jobs_completed);
	fprintf(fout, "Jobs canceled:  %d\n", slurmctld_diag_stats.jobs_canceled);
	fprintf(fout, "Jobs failed:    %d\n", slurmctld_diag_stats.jobs_failed);
	fprintf(fout, "\nMain schedule statistics (microseconds):\n");
	fprintf(fout, "\tLast cycle:   %u\n", slurmctld_diag_stats.schedule_cycle_last);
	fprintf(fout, "\tMax cycle:    %u\n", slurmctld_diag_stats.schedule_cycle_max);
	fprintf(fout, "\tTotal cycles: %u\n", slurmctld_diag_stats.schedule_cycle_counter);
	if (slurmctld_diag_stats.schedule_cycle_counter > 0) {
		fprintf(fout, "\tMean cycle:   %u\n",
				slurmctld_diag_stats.schedule_cycle_sum / slurmctld_diag_stats.schedule_cycle_counter);
		fprintf(fout, "\tMean depth cycle:  %u\n",
				slurmctld_diag_stats.schedule_cycle_depth / slurmctld_diag_stats.schedule_cycle_counter);
	}
	if ((now - last_proc_req_start) > 60) {
		fprintf(fout, "\tCycles per minute: %u\n",
		       (uint32_t) (slurmctld_diag_stats.schedule_cycle_counter /
		       ((now - last_proc_req_start) / 60)));
	}
	fprintf(fout, "\tLast queue length: %u\n", slurmctld_diag_stats.schedule_queue_len);

	if (slurmctld_diag_stats.bf_active) {
		fprintf(fout, "\nBackfilling stats (WARNING: data obtained"
		       " in the middle of backfilling execution.)\n");
	} else
		fprintf(fout, "\nBackfilling stats\n");

	fprintf(fout, "\tTotal backfilled jobs (since last slurm start): %u\n",
		   slurmctld_diag_stats.backfilled_jobs);
	fprintf(fout, "\tTotal backfilled jobs (since last stats cycle start): %u\n",
	       slurmctld_diag_stats.last_backfilled_jobs);
	fprintf(fout, "\tTotal cycles: %u\n", slurmctld_diag_stats.bf_cycle_counter);
	fprintf(fout, "\tLast cycle when: %s", ctime(&slurmctld_diag_stats.bf_when_last_cycle));
	fprintf(fout, "\tLast cycle: %u\n", slurmctld_diag_stats.bf_cycle_last);
	fprintf(fout, "\tMax cycle:  %u\n", slurmctld_diag_stats.bf_cycle_max);
	if (slurmctld_diag_stats.bf_cycle_counter > 0) {
		fprintf(fout, "\tMean cycle: %"PRIu64"\n",
			slurmctld_diag_stats.bf_cycle_sum / slurmctld_diag_stats.bf_cycle_counter);
	}
	fprintf(fout, "\tLast depth cycle: %u\n", slurmctld_diag_stats.bf_last_depth);
	fprintf(fout, "\tLast depth cycle (try sched): %u\n", slurmctld_diag_stats.bf_last_depth_try);
	if (slurmctld_diag_stats.bf_cycle_counter > 0) {
		fprintf(fout, "\tDepth Mean: %u\n",
				slurmctld_diag_stats.bf_depth_sum / slurmctld_diag_stats.bf_cycle_counter);
		fprintf(fout, "\tDepth Mean (try depth): %u\n",
				slurmctld_diag_stats.bf_depth_try_sum / slurmctld_diag_stats.bf_cycle_counter);
	}
	fprintf(fout, "\tLast queue length: %u\n", slurmctld_diag_stats.bf_queue_len);
	if (slurmctld_diag_stats.bf_cycle_counter > 0) {
		fprintf(fout, "\tQueue length mean: %u\n",
				slurmctld_diag_stats.bf_queue_len_sum / slurmctld_diag_stats.bf_cycle_counter);
	}
	fclose(fout);
}

static double sim_sprio_get_priority(priority_factors_object_t *prio_factors)
{
	int i = 0;
	double priority = prio_factors->priority_age
		+ prio_factors->priority_fs
		+ prio_factors->priority_js
		+ prio_factors->priority_part
		+ prio_factors->priority_qos
		- (double)((int64_t)prio_factors->nice - NICE_OFFSET);

	for (i = 0; i < prio_factors->tres_cnt; i++) {
		if (!prio_factors->priority_tres[i])
			continue;
		priority += prio_factors->priority_tres[i];
	}

	/* Priority 0 is reserved for held jobs */
        if (priority < 1)
                priority = 1;

	return priority;
}

/*printout like from sdiag*/
void sim_sprio()
{
	if(slurm_sim_conf->sprio_file_out==NULL)
		return;

	slurm_ctl_conf_t *slurm_conf=&slurmctld_conf;
	char *prio_type   = xstrdup(slurm_conf->priority_type);

	if (xstrcasecmp(prio_type, "priority/basic") == 0) {
		FILE *fout=fopen(slurm_sim_conf->sprio_file_out,"at");
		fprintf (fout, "You are not running a supported "
			 "priority plugin\n(%s).\n"
			 "Only 'priority/multifactor' is supported.\n",
			 prio_type);
		fclose(fout);
		return;
	}

	priority_factors_request_msg_t req_msg;
	memset(&req_msg, 0, sizeof(priority_factors_request_msg_t));

	List priority_factors_list=priority_g_get_priority_factors_list(&req_msg,0);


	FILE *fout=fopen(slurm_sim_conf->sprio_file_out,"at");
	if(fout==NULL)
		return;

	time_t now = time(NULL);

	fprintf(fout, "###############################################################################\n");
	fprintf(fout, "t: %s", ctime(&now));
	fprintf(fout, "          JOBID     USER   PRIORITY        AGE  FAIRSHARE    JOBSIZE  PARTITION        QOS        NICE                 TRES\n");

	if(priority_factors_list!=NULL){
		ListIterator itr;

		itr = list_iterator_create(priority_factors_list);
		priority_factors_object_t * job;
		while ((job = list_next(itr))) {
			char *values = xstrdup("");
			int i = 0;

			for (i = 0; i < job->tres_cnt; i++) {
				if (!job->priority_tres[i])
					continue;
				if (values[0])
					xstrcat(values, ",");
				xstrfmtcat(values, "%s=%.2f", job->tres_names[i],
					   job->priority_tres[i]/job->tres_weights[i]);
			}


			fprintf(fout,"%15u %8s %10.0f %10.0f %10.0f %10.0f %10.0f %10.0f %11ld %20s\n",
					job->job_id,
					uid_to_string_cached(job->user_id),
					sim_sprio_get_priority(job),
					job->priority_age,
					job->priority_fs,
					job->priority_js,
					job->priority_part,
					job->priority_qos,
					(int64_t)job->nice - NICE_OFFSET,
					values);
			xfree(values);
		}
		list_iterator_destroy(itr);
		FREE_NULL_LIST(priority_factors_list);
	}

	xfree(prio_type);
	fclose(fout);
}

static int _sim_print_str(char *str, int width, bool right, bool cut_output)
{
	char format[64];
	char temp[64];
	int printed = 0;

	if (right == true && width != 0)
		snprintf(format, 64, "%%%ds", width);
	else if (width != 0)
		snprintf(format, 64, "%%.%ds", width);
	else {
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}

	if ((width == 0) || (cut_output == false)) {
		snprintf(temp,64,format, str);
		snprintf(str,64,"%s",temp);
	} else {
		char temp[width + 1];
		snprintf(temp, width + 1, format, str);
		snprintf(str,64,"%s",temp);
	}

	while (printed++ < width)
		printf(" ");

	return printed;
}
int _sim_print_secs(char *str, long time, int width, bool right, bool cut_output)
{
	long days, hours, minutes, seconds;

	seconds =  time % 60;
	minutes = (time / 60)   % 60;
	hours   = (time / 3600) % 24;
	days    =  time / 86400;

	if ((time < 0) || (time > (365 * 24 * 3600)))
		snprintf(str, 64, "INVALID");
	else if (days)
		snprintf(str, 64,
			 "%ld-%2.2ld:%2.2ld:%2.2ld",
			 days, hours, minutes, seconds);
	else if (hours)
		snprintf(str, 64,
			 "%ld:%2.2ld:%2.2ld",
			 hours, minutes, seconds);
	else
		snprintf(str, 64,
			 "%ld:%2.2ld",
			 minutes, seconds);

	_sim_print_str(str, width, right, cut_output);
	return SLURM_SUCCESS;
}
/*printout like from sinfo*/
void sim_sinfo()
{
	if(slurm_sim_conf->sinfo_file_out==NULL)
		return;
	FILE *fout=fopen(slurm_sim_conf->sinfo_file_out,"at");
	if(fout==NULL)
		return;

	time_t now = time(NULL);
	struct node_record *node_ptr;

	fprintf(fout, "###############################################################################\n");
	fprintf(fout, "t: %s", ctime(&now));
	fprintf(fout, "PARTITION AVAIL  TIMELIMIT  NODES  STATE NODELIST\n");

	ListIterator part_iterator = list_iterator_create(part_list);
	struct part_record *part_ptr;
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		//printf("Partition: %s\n",part_ptr->name);

		hostlist_t hl=bitmap2hostlist(part_ptr->node_bitmap);

		char part_avail[64];
		if (part_ptr->state_up == PARTITION_UP)
			snprintf(part_avail, 64, "up");
		else if (part_ptr->state_up == PARTITION_DOWN)
			snprintf(part_avail, 64, "down");
		else if (part_ptr->state_up == PARTITION_DRAIN)
			snprintf(part_avail, 64, "drain");
		else if (part_ptr->state_up == PARTITION_INACTIVE)
			snprintf(part_avail, 64, "inactive");
		else
			snprintf(part_avail, 64, "unknown");

		char timelimit[64];
		if(part_ptr->max_time == INFINITE)
			snprintf(timelimit, 64, "infinite");
		else
			_sim_print_secs(timelimit,(part_ptr->max_time * 60L),
						10, 1, 1);


		hostlist_t nodes_unk=hostlist_create(NULL);
		hostlist_t nodes_down=hostlist_create(NULL);
		hostlist_t nodes_idle=hostlist_create(NULL);
		hostlist_t nodes_alloc=hostlist_create(NULL);
		hostlist_t nodes_err=hostlist_create(NULL);
		hostlist_t nodes_mix=hostlist_create(NULL);
		hostlist_t nodes_fut=hostlist_create(NULL);

		//
		hostlist_iterator_t itr = NULL;
		char *host = NULL;
		itr = hostlist_iterator_create(hl);
		while((host = hostlist_next(itr))) {
			//printf("node %s\n",host);
			struct node_record *node_ptr=find_node_record(host);
			if(node_ptr==NULL)continue;

			if(IS_NODE_UNKNOWN(node_ptr))
				hostlist_push_host(nodes_unk,node_ptr->name);
			if(IS_NODE_DOWN(node_ptr))
				hostlist_push_host(nodes_down,node_ptr->name);
			if(IS_NODE_IDLE(node_ptr))
				hostlist_push_host(nodes_idle,node_ptr->name);
			if(IS_NODE_ALLOCATED(node_ptr))
				hostlist_push_host(nodes_alloc,node_ptr->name);
			if(IS_NODE_ERROR(node_ptr))
				hostlist_push_host(nodes_err,node_ptr->name);
			if(IS_NODE_MIXED(node_ptr))
				hostlist_push_host(nodes_mix,node_ptr->name);
			if(IS_NODE_FUTURE(node_ptr))
				hostlist_push_host(nodes_fut,node_ptr->name);
		}
		hostlist_iterator_destroy(itr);

		if(hostlist_count(nodes_unk)>0){
			char *tmp = hostlist_ranged_string_xmalloc(nodes_unk);
			fprintf(fout,"%9s %5s %10s %6d    unk %s\n",part_ptr->name,part_avail,timelimit,
				hostlist_count(nodes_unk), tmp);
			xfree(tmp);
		}
		if(hostlist_count(nodes_down)>0){
			char *tmp = hostlist_ranged_string_xmalloc(nodes_down);
			fprintf(fout,"%9s %5s %10s %6d   down %s\n",part_ptr->name,part_avail,timelimit,
				hostlist_count(nodes_down), tmp);
			xfree(tmp);
		}
		if(hostlist_count(nodes_idle)>0){
			char *tmp = hostlist_ranged_string_xmalloc(nodes_idle);
			fprintf(fout,"%9s %5s %10s %6d   idle %s\n",part_ptr->name,part_avail,timelimit,
				hostlist_count(nodes_idle), tmp);
			xfree(tmp);
		}
		if(hostlist_count(nodes_alloc)>0){
			char *tmp = hostlist_ranged_string_xmalloc(nodes_alloc);
			fprintf(fout,"%9s %5s %10s %6d  alloc %s\n",part_ptr->name,part_avail,timelimit,
				hostlist_count(nodes_alloc), tmp);
			xfree(tmp);
		}
		if(hostlist_count(nodes_err)>0){
			char *tmp = hostlist_ranged_string_xmalloc(nodes_err);
			fprintf(fout,"%9s %5s %10s %6d    err %s\n",part_ptr->name,part_avail,timelimit,
				hostlist_count(nodes_err), tmp);
			xfree(tmp);
		}
		if(hostlist_count(nodes_mix)>0){
			char *tmp = hostlist_ranged_string_xmalloc(nodes_mix);
			fprintf(fout,"%9s %5s %10s %6d    mix %s\n",part_ptr->name,part_avail,timelimit,
				hostlist_count(nodes_mix), tmp);
			xfree(tmp);
		}
		if(hostlist_count(nodes_fut)>0){
			char *tmp = hostlist_ranged_string_xmalloc(nodes_fut);
			fprintf(fout,"%9s %5s %10s %6d future %s\n",part_ptr->name,part_avail,timelimit,
				hostlist_count(nodes_fut), tmp);
			xfree(tmp);
		}


		hostlist_destroy(nodes_fut);
		hostlist_destroy(nodes_mix);
		hostlist_destroy(nodes_err);
		hostlist_destroy(nodes_alloc);
		hostlist_destroy(nodes_idle);
		hostlist_destroy(nodes_down);
		hostlist_destroy(nodes_unk);


		hostlist_destroy(hl);
		//hl = hostlist_create(part_ptr->nodes);
		//pos = hostlist_find(hl, node_msg->node_array[0].name);
		//hostlist_destroy(hl);
	}
	/*int inx;
	struct node_record *node_ptr = node_record_table_ptr;
	for (inx = 0; inx < node_record_count; inx++, node_ptr++) {

	}
	hostlist_t*/
	fclose(fout);
}


/*printout like from squeue*/
void sim_squeue()
{
	if(slurm_sim_conf->squeue_file_out==NULL)
		return;
	FILE *fout=fopen(slurm_sim_conf->squeue_file_out,"at");
	if(fout==NULL)
		return;

	time_t now = time(NULL);

	fprintf(fout, "###############################################################################\n");
	fprintf(fout, "t: %s", ctime(&now));
	fprintf(fout, "             JOBID PARTITION     NAME     USER ST       TIME  NODES NODELIST(REASON)\n");
	struct job_record *job;

	ListIterator job_iterator;
	job_iterator = list_iterator_create(job_list);
	while ((job = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_COMPLETING(job)
				   && (IS_JOB_PENDING(job)
				       || IS_JOB_TIMEOUT(job)
				       || IS_JOB_DEADLINE(job)
				       || IS_JOB_FAILED(job))) {
			fprintf(fout, "%18u %9s %8s %8s", job->job_id,job->partition,
							job->name,uid_to_string_cached(job->user_id));
			fprintf(fout," %2s       0:00 %6d", job_state_string_compact(job->job_state),job->node_cnt_wag);
			if(job->state_desc!=NULL){
				fprintf(fout," (%s)\n",job->state_desc);
			}else{
				fprintf(fout," (%s)\n",job_reason_string(job->state_reason));
			}
		}

	}
	list_iterator_destroy(job_iterator);

	job_iterator = list_iterator_create(job_list);
	while ((job = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_COMPLETING(job)
						   && (IS_JOB_PENDING(job)
						       || IS_JOB_TIMEOUT(job)
						       || IS_JOB_DEADLINE(job)
						       || IS_JOB_FAILED(job))) {
			continue;
		}
		if (!IS_JOB_COMPLETE(job)){
        	fprintf(fout, "%18u %9s %8s %8s", job->job_id,job->partition,
							job->name,uid_to_string_cached(job->user_id));
        	char str[32];
        	_sim_print_secs(str,now-job->start_time,10,1,0);
			fprintf(fout," %2s %10s %6d %s\n", job_state_string_compact(job->job_state),str,job->node_cnt,job->nodes);
        }
	}
	list_iterator_destroy(job_iterator);



	fclose(fout);
}
void sim_submit_jobs()
{
	static int failed_submissions=0;
	/* Now checking if a new job needs to be submitted */
	while (trace_head) {
		/*
		 * Uhmm... This is necessary if a large number of jobs
		 * are submitted at the same time but it seems
		 * to have an impact on determinism
		 */

		if (*(sim_utime)/1000000 >= trace_head->submit) {

			/*job_trace_t *temp_ptr;*/

			debug("[%d] time_mgr--current simulated time: "
				   "%lu\n", __LINE__, *(sim_utime)/1000000);

			insert_in_queue_trace_record(trace_head);


			if (_sim_submit_job (trace_head) < 0)
				++failed_submissions;

			trace_head = trace_head->next;

			sim_job_sched_cnt++;
			/*if (temp_ptr) xfree(temp_ptr);*/
		} else {
			/*
			 * The job trace list is ordered in time so
			 * there is nothing else to do.
			 */
			break;
		}
	}
}


extern int sim_controller()
{
	//print conf
	sim_print_sim_conf();

	//set time
	sim_pause_clock();
	sim_set_new_time(slurm_sim_conf->time_start*(uint64_t)1000000);

	//read job traces
	sim_read_job_trace(slurm_sim_conf->jobs_trace_file);
	if(trace_head!=NULL){
		sim_set_new_time((trace_head->submit-slurm_sim_conf->start_seconds_before_first_job)*(uint64_t)1000000);
	}
	sim_resume_clock();
	uint64_t init_utime=*sim_utime;

	//kill threads which are not impotent for simulation
	_sim_kill_not_critical_threads();

	//register nodes
	//_sim_register_nodes();
	_sim_register_nodes_from_frontend_slurmd();

	sim_resume_clock();

	uint64_t time_to_terminate=0;
	int run_scheduler=0;
	int failed_submissions=0;

	int jobs_scheduled=0;
	int jobs_scheduled_by_plugin=0;

	static time_t last_db_inx_handler_call = 0;


	//uint32_t schedule_last_runtime=*current_sim;
	int run_scheduler_plugin=0;
	int schedule_plugin_sleeptype=1;//0-short,1-long, 2-very long
	int schedule_plugin_next_sleeptype=1;
	uint32_t schedule_plugin_sleep[3]={1,30,300};

	uint32_t schedule_plugin_last_runtime=0;
	//uint32_t schedule_plugin_next_runtime=*current_sim+schedule_plugin_sleep[schedule_plugin_sleeptype];
	uint32_t schedule_plugin_last_depth_try=0;
	//int schedule_plugin_short_sleep=false;
	time_t cur_time;
	schedule_plugin_run_once();
	schedule_plugin_last_runtime=time(NULL);


	uint32_t next_sprio=0;
	uint32_t next_sinfo=0;
	uint32_t next_squeue=0;

    char ctime_buff[128];
	while(1)
	{
		cur_time=time(NULL);

		//strftime (ctime_buff, 100, "%Y-%m-%d %H:%M:%S.000", localtime (&cur_time));
		//debug("time_mgr: current %lu and sinse start %.3f",
		//		*(sim_utime), (*(sim_utime)-init_utime)*0.000001);

		int new_job_submitted=0;
		int job_finished=0;
		//int scheduler_ran=0;
		//info("SIM main loop\n");

		/* Do we have to end simulation in this cycle?
		 * sim_end_point=0 run indefinitely
		 * sim_end_point=1 exit when all jobs are done
		 * sim_end_point>1 run till specified time
		 * */
		if (slurm_sim_conf->time_stop==1){
			/*terminate if all jobs are complete*/
			if(trace_head==NULL){
				//no more jobs to submit
				if(head_simulator_event==NULL){
					//all jobs in queue are done
					if(time_to_terminate==0){
						//No more jobs to run, terminating simulation, but will wait for 1 more minute
						time_to_terminate=*sim_utime+60000000;
					}
					else if(*sim_utime>time_to_terminate){
						info("No more jobs to run, terminating simulation");
						exit(0);
					}
				}
				else if(time_to_terminate>0){
					//change my mind no termination
					time_to_terminate=0;
				}
			}
		}
		else if (slurm_sim_conf->time_stop>1){
			/*terminate if reached end time*/
			if(slurm_sim_conf->time_stop <= *sim_utime/1000000) {
				break;
			}
		}

		//submit jobs if needed
		sim_submit_jobs();


		//check if jobs done
		sim_process_finished_jobs();

		//check if jobs need tp be cancelled
		sim_cancel_jobs();

		//run priority decay plugin
		sim_run_priority_decay();

		//run scheduler
		jobs_scheduled=sim_schedule();


		//try to run backfill
		schedule_plugin_run_once();

		//last_db_inx_handler_call
		if(sim_db_inx_handler_call_once!=NULL && last_db_inx_handler_call-time(NULL)>5){
			(*sim_db_inx_handler_call_once)();
			last_db_inx_handler_call=time(NULL);
		}

		//do outputs
		cur_time=time(NULL);
		if(slurm_sim_conf->sprio_period!=0 && slurm_sim_conf->sprio_file_out!=NULL && cur_time>next_sprio){
			next_sprio=(cur_time/slurm_sim_conf->sprio_period+1)*slurm_sim_conf->sprio_period;
			sim_sprio();
		}
		cur_time=time(NULL);
		if(slurm_sim_conf->sinfo_period!=0 && slurm_sim_conf->sinfo_file_out!=NULL && cur_time>next_sinfo){
			next_sinfo=(cur_time/slurm_sim_conf->sinfo_period+1)*slurm_sim_conf->sinfo_period;
			sim_sinfo();
		}
		cur_time=time(NULL);
		if(slurm_sim_conf->squeue_period!=0 && slurm_sim_conf->squeue_file_out!=NULL && cur_time>next_squeue){
			next_squeue=(cur_time/slurm_sim_conf->squeue_period+1)*slurm_sim_conf->squeue_period;
			sim_squeue();
		}

		//update time
		if(new_job_submitted+job_finished+run_scheduler==0){
			sim_pause_clock();
			sim_incr_clock(slurm_sim_conf->time_step);
			sim_resume_clock();
		}

	}

	return 0;
}


void sim_mini_loop(){
	uint64_t cur_time= get_sim_utime();
	static uint64_t next_run_mini_loop = 0;

	if(next_run_mini_loop<cur_time){

		//submit jobs if needed
		sim_submit_jobs();

		//check if jobs done
		sim_process_finished_jobs();

		//check if jobs need tp be cancelled
		sim_cancel_jobs();

		//run priority decay plugin
		sim_run_priority_decay();

		//run scheduler
		sim_schedule();

		next_run_mini_loop=get_sim_utime()+500000;
	}
}

int usleep (__useconds_t __useconds)
{
	return __useconds;
}
unsigned int sleep (unsigned int __seconds)
{
	return __seconds;
}

int pthread_mutex_lock (pthread_mutex_t *__mutex)
{
	return 0;
}
int pthread_mutex_unlock (pthread_mutex_t *__mutex)
{
	return 0;
}
/*
 * "Constructor" function to be called before the main of each Slurm
 * entity (e.g. slurmctld, slurmd and commands).
 */

void __attribute__ ((constructor)) sim_ctrl_init(void)
{
	sim_ctrl=1;
}
#endif
