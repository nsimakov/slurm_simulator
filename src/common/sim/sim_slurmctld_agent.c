#include "src/slurmctld/agent.c"

/*
extern void __real_agent_queue_request(agent_arg_t *agent_arg_ptr);
extern void __wrap_agent_queue_request(agent_arg_t *agent_arg_ptr)
{
	//kill_job_msg_t * kill_job;
//	batch_job_launch_msg_t *launch_msg_ptr;
//	job_record_t *job_ptr;
//	time_t now;
	//queued_request_t *queued_req_ptr = NULL;
	//__real_agent_queue_request(agent_arg_ptr);
	//return;

	//char *hostname;
	debug("Sim: __wrap_agent_queue_request msg_type=%s", rpc_num2string(agent_arg_ptr->msg_type));
	__real_agent_queue_request(agent_arg_ptr);

	switch(agent_arg_ptr->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		//__real_agent_queue_request(agent_arg_ptr);
//		launch_msg_ptr = (batch_job_launch_msg_t *)agent_arg_ptr->msg_args;
//		job_ptr = find_job_record(launch_msg_ptr->job_id);
//		now = time(NULL);
//		job_ptr->start_time = now;
		__real_agent_queue_request(agent_arg_ptr);
		break;
	case REQUEST_KILL_TIMELIMIT:
		//kill_job = (kill_job_msg_t*)agent_arg_ptr->msg_args;
		//hostname = hostlist_shift(agent_arg_ptr->hostlist);
		// REQUEST_COMPLETE_BATCH_SCRIPT
		//job_complete(kill_job->job_id, kill_job->job_uid, false, false, SLURM_SUCCESS);
		// MESSAGE_EPILOG_COMPLETE
		//job_epilog_complete(kill_job->job_id, hostname, SLURM_SUCCESS);
		//free(hostname);
		__real_agent_queue_request(agent_arg_ptr);
		break;
	default:
		__real_agent_queue_request(agent_arg_ptr);
		break;
	}

}*/
