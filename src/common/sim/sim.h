#ifndef _SIM_H
#define _SIM_H

/******************************************************************************
 * Simulator Configuration Parameters
 ******************************************************************************/

/* Slurm simulator configuration parameters */
typedef struct slurm_sim_conf {
	uint32_t	time_start;	/* initial starting time will be overwritten by time from first job */
	uint32_t	time_stop;	/* final time when simulation should stop, 0-never stop, 1-stop after all jobs are done*/
	uint32_t    seconds_before_first_job;
	double      clock_scaling;
	/* shared memory name, used to sync slurmdbd and slurmctrld, should be
	 * different if multiple simulation is running at same time */
	char *      shared_memory_name;
	char *      events_file;
	long    time_after_all_events_done;
} slurm_sim_conf_t;

/* simulator configuration */
extern slurm_sim_conf_t *slurm_sim_conf;

/* read simulator configuration file */
extern int sim_read_sim_conf(void);

/* print simulator configuration */
extern int sim_print_sim_conf(void);


extern int64_t get_sim_utime();


/******************************************************************************
 * Simulation Events
 ******************************************************************************/
typedef enum {
	SIM_NODE_REGISTRATION = 1001,
	SIM_SUBMIT_BATCH_JOB,
	SIM_COMPLETE_BATCH_SCRIPT,
	SIM_CANCEL_JOB,
} sim_event_type_t;

typedef struct sim_event_submit_batch_job {
	int walltime; /*actual walltime*/
	uint32_t job_id;	/* job ID */
	char **argv;
	int argc;
} sim_event_submit_batch_job_t;

typedef struct sim_event {
	int64_t when; /* time of event in usec*/
	struct sim_event *next;
	struct sim_event *previous;
	sim_event_type_t type; /* event type */
	void *payload; /* event type */
} sim_event_t;

extern sim_event_t * sim_next_event;

extern void sim_init_events();
extern void sim_print_events();
extern void sim_print_event(sim_event_t * event);

extern void sim_insert_event(int64_t when, int type, void *payload);
extern void sim_insert_event_comp_job(uint32_t job_id);

extern pthread_mutex_t events_mutex;

/******************************************************************************
 * Active Simulated Jobs
 ******************************************************************************/
/* sim_job contain information needed during job being in queue or running */
typedef struct sim_job sim_job_t;
typedef struct sim_job {
	int walltime; /*job duration, INT32_MAX or any large value would results in job running till time limit*/
	uint32_t job_id;	/* job ID */
	int64_t submit_time; /* submit_time in usec*/
	int64_t start_time; /* start_time in usec*/

	sim_job_t *next_sim_job;
	sim_job_t *previous_sim_job;
} sim_job_t;

extern pthread_mutex_t active_job_mutex;

extern void sim_insert_sim_active_job(sim_event_submit_batch_job_t* event_submit_batch_job);
extern int sim_remove_active_sim_job(uint32_t job_id);
extern sim_job_t *sim_find_active_sim_job(uint32_t job_id);
extern void sim_print_active_jobs();

/******************************************************************************
 * Simulated Time
 ******************************************************************************/
extern int64_t get_sim_utime();

#endif
