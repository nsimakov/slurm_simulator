#define main slurmctld_main
#include "src/slurmctld/controller.c"
#undef main

#include "src/common/sim/sim.h"


extern slurmctld_config_t slurmctld_config;

extern int sim_controller();
static void *_sim_slurmctld_background(void *no_data);

void __attribute__ ((constructor)) sim_init_ctld(void)
{
	info("Sim: Slurm simulator init (sim_init_ctld).");
}

extern void __real_agent_init(void);
extern void __wrap_agent_init(void)
{
	info("Sim: agent_init.");
	__real_agent_init();
}

extern int __real_fed_mgr_init(void *db_conn);
extern int __wrap_fed_mgr_init(void *db_conn)
{
	return SLURM_SUCCESS;
}

extern int __real_pthread_create (pthread_t *newthread,
		const pthread_attr_t *attr,
		void *(*start_routine) (void *),
		void *arg);

extern int __wrap_pthread_create (pthread_t *newthread,
		const pthread_attr_t *attr,
		void *(*start_routine) (void *),
		void *arg)
{
	if (&slurmctld_config.thread_id_rpc == newthread) {
		debug("Sim: thread_id_rpc.");
	} else if (&slurmctld_config.thread_id_sig == newthread) {
		debug("Sim: thread_id_sig.");
		//sim_controller();
		_sim_slurmctld_background(NULL);
		return 0;
	} else if (&slurmctld_config.thread_id_save) {
		debug("Sim: thread_id_save ... skip.");
		return 0;
	} else if (&slurmctld_config.thread_id_power) {
		debug("Sim: thread_id_power ... skip.");
		return 0;
	} else if (&slurmctld_config.thread_id_purge_files) {
		debug("Sim: thread_id_purge_files ... skip.");
		return 0;
	}
	return __real_pthread_create(newthread, attr, start_routine, arg);
}

// in sim don;t block ctrl-c
int __real_xsignal_block(int sigarray[]);
int __wrap_xsignal_block(int sigarray[]){
	return 0;
}

int sim_init_slurmd(int argc, char **argv);

int
main (int argc, char **argv)
{
	daemonize = 0;

	sim_init_slurmd(argc, argv);

	slurmctld_main(argc, argv);

	debug("%d", controller_sigarray[0]);
}

extern int sim_registration_engine();
extern int sim_controller()
{
	info("Sim: sim_controller.");
	// register nodes
	//sim_registration_engine();


	//while(1){
	//	sleep(1);
	//}
	return 0;
}

/*
 * _sim_slurmctld_background - process slurmctld background activities
 *	purge defunct job records, save state, schedule jobs, and
 *	ping other nodes
 */
static void *_sim_slurmctld_background(void *no_data)
{
	static time_t last_sched_time;
	static time_t last_full_sched_time;
	static time_t last_checkpoint_time;
	static time_t last_group_time;
	static time_t last_health_check_time;
	static time_t last_acct_gather_node_time;
	static time_t last_ext_sensors_time;
	static time_t last_no_resp_msg_time;
	static time_t last_ping_node_time = (time_t) 0;
	static time_t last_ping_srun_time;
	static time_t last_purge_job_time;
	static time_t last_resv_time;
	static time_t last_timelimit_time;
	static time_t last_assert_primary_time;
	static time_t last_trigger;
	static time_t last_node_acct;
	static time_t last_ctld_bu_ping;
	static time_t last_uid_update;
	static time_t last_reboot_msg_time;
	time_t now;
	int no_resp_msg_interval, ping_interval, purge_job_interval;
	int i;
	uint32_t job_limit;
	DEF_TIMERS;

	/* Locks: Read config */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, read job */
	slurmctld_lock_t job_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock2 = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Read config, write job, write node
	 * (Might kill jobs on nodes set DOWN) */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Write node */
	slurmctld_lock_t node_write_lock2 = {
		NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Write partition */
	slurmctld_lock_t part_write_lock = {
		NO_LOCK, NO_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK };
	/* Locks: Read job and node */
	slurmctld_lock_t job_node_read_lock = {
		NO_LOCK, READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/*
	 * purge_old_job modifes jobs and reads conf info. It can also
	 * call re_kill_job(), which can modify nodes and reads fed info.
	 */
	slurmctld_lock_t purge_job_locks = {
		.conf = READ_LOCK, .job = WRITE_LOCK,
		.node = WRITE_LOCK, .fed = READ_LOCK
	};

	/* Let the dust settle before doing work */
	now = time(NULL);
	last_sched_time = last_full_sched_time = now;
	last_checkpoint_time = last_group_time = now;
	last_purge_job_time = last_trigger = last_health_check_time = now;
	last_timelimit_time = last_assert_primary_time = now;
	last_no_resp_msg_time = last_resv_time = last_ctld_bu_ping = now;
	last_uid_update = last_reboot_msg_time = now;
	last_acct_gather_node_time = last_ext_sensors_time = now;


	last_ping_srun_time = now;
	last_node_acct = now;
	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	debug3("_slurmctld_background pid = %u", getpid());

	while (1) {
		for (i = 0; ((i < 10) && (slurmctld_config.shutdown_time == 0));
		     i++) {
			usleep(100000);
		}

		now = time(NULL);
		START_TIMER;

		if (slurmctld_conf.slurmctld_debug <= 3)
			no_resp_msg_interval = 300;
		else if (slurmctld_conf.slurmctld_debug == 4)
			no_resp_msg_interval = 60;
		else
			no_resp_msg_interval = 1;

		if ((slurmctld_conf.min_job_age > 0) &&
		    (slurmctld_conf.min_job_age < PURGE_JOB_INTERVAL)) {
			/* Purge jobs more quickly, especially for high job flow */
			purge_job_interval = MAX(10, slurmctld_conf.min_job_age);
		} else
			purge_job_interval = PURGE_JOB_INTERVAL;

		if (slurmctld_conf.slurmd_timeout) {
			/* We ping nodes that haven't responded in SlurmdTimeout/3,
			 * but need to do the test at a higher frequency or we might
			 * DOWN nodes with times that fall in the gap. */
			ping_interval = slurmctld_conf.slurmd_timeout / 3;
		} else {
			/* This will just ping non-responding nodes
			 * and restore them to service */
			ping_interval = 100;	/* 100 seconds */
		}

		if (!last_ping_node_time) {
			last_ping_node_time = now + (time_t)MIN_CHECKIN_TIME -
					      ping_interval;
		}

		if (slurmctld_config.shutdown_time) {
			struct timespec ts = {0, 0};
			struct timeval now;
			int exp_thread_cnt =
				slurmctld_config.resume_backup ? 1 : 0;
			/* wait for RPC's to complete */
			gettimeofday(&now, NULL);
			ts.tv_sec = now.tv_sec + CONTROL_TIMEOUT;
			ts.tv_nsec = now.tv_usec * 1000;

			slurm_mutex_lock(&slurmctld_config.thread_count_lock);
			while (slurmctld_config.server_thread_count >
			       exp_thread_cnt) {
				slurm_cond_timedwait(
					&slurmctld_config.thread_count_cond,
					&slurmctld_config.thread_count_lock,
					&ts);
			}
			if (slurmctld_config.server_thread_count >
			    exp_thread_cnt) {
				info("shutdown server_thread_count=%d",
				     slurmctld_config.server_thread_count);
			}
			slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

			if (!report_locks_set()) {
				info("Saving all slurm state");
				save_all_state();
			} else {
				error("Semaphores still set after %d seconds, "
				      "can not save state", CONTROL_TIMEOUT);
			}
			break;
		}

		if (difftime(now, last_resv_time) >= 5) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_resv_time = now;
			if (set_node_maint_mode(false) > 0)
				queue_job_scheduler();
			unlock_slurmctld(node_write_lock);
		}

		if (difftime(now, last_no_resp_msg_time) >=
		    no_resp_msg_interval) {
			lock_slurmctld(node_write_lock2);
			now = time(NULL);
			last_no_resp_msg_time = now;
			node_no_resp_msg();
			unlock_slurmctld(node_write_lock2);
		}

		if (difftime(now, last_timelimit_time) >= PERIODIC_TIMEOUT) {
			lock_slurmctld(job_write_lock);
			now = time(NULL);
			last_timelimit_time = now;
			debug2("Testing job time limits and checkpoints");
			job_time_limit();
			job_resv_check();
			unlock_slurmctld(job_write_lock);

			lock_slurmctld(node_write_lock);
			check_reboot_nodes();
			unlock_slurmctld(node_write_lock);
		}

		if (slurmctld_conf.health_check_interval &&
		    (difftime(now, last_health_check_time) >=
		     slurmctld_conf.health_check_interval) &&
		    is_ping_done()) {
			lock_slurmctld(node_write_lock);
			if (slurmctld_conf.health_check_node_state &
			     HEALTH_CHECK_CYCLE) {
				/* Call run_health_check() on each cycle */
			} else {
				now = time(NULL);
				last_health_check_time = now;
			}
			run_health_check();
			unlock_slurmctld(node_write_lock);
		}

		if (slurmctld_conf.acct_gather_node_freq &&
		    (difftime(now, last_acct_gather_node_time) >=
		     slurmctld_conf.acct_gather_node_freq) &&
		    is_ping_done()) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_acct_gather_node_time = now;
			update_nodes_acct_gather_data();
			unlock_slurmctld(node_write_lock);
		}

		if (slurmctld_conf.ext_sensors_freq &&
		    (difftime(now, last_ext_sensors_time) >=
		     slurmctld_conf.ext_sensors_freq) &&
		    is_ping_done()) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_ext_sensors_time = now;
			ext_sensors_g_update_component_data();
			unlock_slurmctld(node_write_lock);
		}

		if (((difftime(now, last_ping_node_time) >= ping_interval) ||
		     ping_nodes_now) && is_ping_done()) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_ping_node_time = now;
			ping_nodes_now = false;
			ping_nodes();
			unlock_slurmctld(node_write_lock);
		}

		if (slurmctld_conf.inactive_limit &&
		    ((now - last_ping_srun_time) >=
		     (slurmctld_conf.inactive_limit / 3))) {
			lock_slurmctld(job_read_lock);
			now = time(NULL);
			last_ping_srun_time = now;
			debug2("Performing srun ping");
			srun_ping();
			unlock_slurmctld(job_read_lock);
		}

		if (want_nodes_reboot && (now > last_reboot_msg_time)) {
			lock_slurmctld(node_write_lock);
			now = time(NULL);
			last_reboot_msg_time = now;
			_queue_reboot_msg();
			unlock_slurmctld(node_write_lock);
		}

		/* Process any pending agent work */
		agent_trigger(RPC_RETRY_INTERVAL, true);

		if (slurmctld_conf.group_time &&
		    (difftime(now, last_group_time)
		     >= slurmctld_conf.group_time)) {
			lock_slurmctld(part_write_lock);
			now = time(NULL);
			last_group_time = now;
			load_part_uid_allow_list(slurmctld_conf.group_force);
			unlock_slurmctld(part_write_lock);
			group_cache_cleanup();
		}

		if (difftime(now, last_purge_job_time) >= purge_job_interval) {
			/*
			 * If backfill is running, it will have a List of
			 * job_record pointers which could include this
			 * job. Skip over in that case to prevent
			 * _attempt_backfill() from potentially dereferencing an
			 * invalid pointer.
			 */
			slurm_mutex_lock(&check_bf_running_lock);
			if (!slurmctld_diag_stats.bf_active) {
				lock_slurmctld(purge_job_locks);
				now = time(NULL);
				last_purge_job_time = now;
				debug2("Performing purge of old job records");
				purge_old_job();
				unlock_slurmctld(purge_job_locks);
			}
			slurm_mutex_unlock(&check_bf_running_lock);
		}

		job_limit = NO_VAL;
		if (difftime(now, last_full_sched_time) >= sched_interval) {
			slurm_mutex_lock(&sched_cnt_mutex);
			/* job_limit = job_sched_cnt;	Ignored */
			job_limit = INFINITE;
			job_sched_cnt = 0;
			slurm_mutex_unlock(&sched_cnt_mutex);
			last_full_sched_time = now;
		} else {
			slurm_mutex_lock(&sched_cnt_mutex);
			if (job_sched_cnt &&
			    (difftime(now, last_sched_time) >=
			     batch_sched_delay)) {
				job_limit = 0;	/* Default depth */
				job_sched_cnt = 0;
			}
			slurm_mutex_unlock(&sched_cnt_mutex);
		}
		if (job_limit != NO_VAL) {
			lock_slurmctld(job_write_lock2);
			now = time(NULL);
			last_sched_time = now;
			bb_g_load_state(false);	/* May alter job nice/prio */
			unlock_slurmctld(job_write_lock2);
			if (schedule(job_limit))
				last_checkpoint_time = 0; /* force state save */
			set_job_elig_time();
		}

		if (slurmctld_conf.slurmctld_timeout &&
		    (difftime(now, last_ctld_bu_ping) >
		     slurmctld_conf.slurmctld_timeout)) {
			ping_controllers(true);
			last_ctld_bu_ping = now;
		}

		if (difftime(now, last_trigger) > TRIGGER_INTERVAL) {
			lock_slurmctld(job_node_read_lock);
			now = time(NULL);
			last_trigger = now;
			trigger_process();
			unlock_slurmctld(job_node_read_lock);
		}

		if (difftime(now, last_checkpoint_time) >=
		    PERIODIC_CHECKPOINT) {
			now = time(NULL);
			last_checkpoint_time = now;
			debug2("Performing full system state save");
			save_all_state();
		}

		if (difftime(now, last_node_acct) >= PERIODIC_NODE_ACCT) {
			/* Report current node state to account for added
			 * or reconfigured nodes.  Locks are done
			 * inside _accounting_cluster_ready, don't
			 * lock here. */
			now = time(NULL);
			last_node_acct = now;
			_accounting_cluster_ready();
		}

		if (difftime(now, slurmctld_diag_stats.job_states_ts) >=
		    JOB_COUNT_INTERVAL) {
			lock_slurmctld(job_read_lock);
			_update_diag_job_state_counts();
			unlock_slurmctld(job_read_lock);
		}

		/* Stats will reset at midnight (approx) local time. */
		if (last_proc_req_start == 0) {
			last_proc_req_start = now;
			next_stats_reset = now - (now % 86400) + 86400;
		} else if (now >= next_stats_reset) {
			next_stats_reset = now - (now % 86400) + 86400;
			reset_stats(0);
		}

		/*
		 * Reassert this machine as the primary controller.
		 * A network or security problem could result in
		 * the backup controller assuming control even
		 * while the real primary controller is running.
		 */
		lock_slurmctld(config_read_lock);
		if (slurmctld_primary && slurmctld_conf.slurmctld_timeout &&
		    (difftime(now, last_assert_primary_time) >=
		     slurmctld_conf.slurmctld_timeout)) {
			now = time(NULL);
			last_assert_primary_time = now;
			(void) _shutdown_backup_controller();
		}
		unlock_slurmctld(config_read_lock);

		if (difftime(now, last_uid_update) >= 3600) {
			/*
			 * Make sure we update the uids in the
			 * assoc_mgr if there were any users
			 * with unknown uids at the time of startup.
			 */
			now = time(NULL);
			last_uid_update = now;
			assoc_mgr_set_missing_uids();
		}

		END_TIMER2("_slurmctld_background");
	}

	debug3("_slurmctld_background shutting down");

	return NULL;
}
