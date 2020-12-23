
// include slurm.h first to ignore include in sbatch.c
#include "slurm/slurm.h"



#define main sbatch_main
//#define slurm_submit_batch_job wrap_slurm_submit_batch_job
//extern int wrap_slurm_submit_batch_job(job_desc_msg_t *req,
//				  submit_response_msg_t **resp);
//#define slurm_conf_init(...) {};
//#define log_init(...) {};
#include "src/sbatch/sbatch.c"
#undef main

#include "src/common/sim/sim.h"

//extern int wrap_slurm_submit_batch_job(job_desc_msg_t *req,
//				  submit_response_msg_t **resp) {
//	return 0;
//}

extern void submit_job(sim_event_submit_batch_job_t* event_submit_batch_job)
{
	int sbatch_argc=event_submit_batch_job->argc;
	char **sbatch_argv=event_submit_batch_job->argv;

	//sbatch_main(sbatch_argc, sbatch_argv);
	//return;

	job_desc_msg_t *desc = NULL;
	//submit_response_msg_t *resp = NULL;
	//char *script_name="sim.job";
	char *script_body="#!/bin/bash\nsleep 30\n";
	char *script_name;
	//char *script_body;

	script_name = process_options_first_pass(sbatch_argc, sbatch_argv);

	int script_size = 0, het_job_argc, het_job_argc_off = 0, het_job_inx = 0;
	char **het_job_argv;
	bool more_het_comps = false;

	het_job_argc = sbatch_argc - sbopt.script_argc;
	het_job_argv = sbatch_argv;

	process_options_second_pass(het_job_argc, het_job_argv,
						    &het_job_argc_off, het_job_inx,
						    &more_het_comps, script_name ?
						    xbasename (script_name) : "stdin",
						    script_body, script_size);

	sbatch_env_t *local_env = (sbatch_env_t*)xmalloc(sizeof(sbatch_env_t));
	memcpy(local_env, &het_job_env, sizeof(sbatch_env_t));
	desc = xmalloc(sizeof(job_desc_msg_t));
	slurm_init_job_desc_msg(desc);
	if (_fill_job_desc_from_opts(desc) == -1) {
		error("Can not fill _fill_job_desc_from_opts");
	}
	desc->script = (char *) script_body;

	if(event_submit_batch_job->job_id >0) {
		desc->job_id = event_submit_batch_job->job_id;
	}


	//int rc = SLURM_SUCCESS;
	submit_response_msg_t *resp = NULL;
	slurm_submit_batch_job(desc, &resp);
	//sbatch_main(sbatch_argc, sbatch_argv);
	//desc = xmalloc(sizeof(job_desc_msg_t));

	if(resp != NULL) {
		// insert job to active simulated job list
		if(event_submit_batch_job->job_id==0) {
			pthread_mutex_lock(&events_mutex);
			event_submit_batch_job->job_id = resp->job_id;
			pthread_mutex_unlock(&events_mutex);
		}
		if(event_submit_batch_job->job_id != resp->job_id) {
			error("Job id in event list (%d) does not match to one returned from sbatch (%d)",
					event_submit_batch_job->job_id, resp->job_id);
		}
		sim_insert_sim_active_job(event_submit_batch_job);
	} else {
		error("Job was not submitted!");
	}
}

