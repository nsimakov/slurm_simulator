#ifndef _SIM_H
#define _SIM_H

#ifdef SLURM_SIMULATOR

#include <semaphore.h>
#include "slurm/slurm.h"

/******************************************************************************
 * Simulator Configuration Parameters
 ******************************************************************************/

/* Slurm simulator configuration parameters */
typedef struct slurm_sim_conf {

	uint32_t	time_start;	/* initial starting time will be overwritten by time from first job */
	long        start_seconds_before_first_job;
	uint32_t	time_stop;	/* final time when simulation should stop, 0-nether stop, 1-stop after all jobs are done*/
	uint32_t	time_step;	/* time step for simulation in microseconds */
	uint32_t	after_job_launch_time_increament;
	uint32_t	bf_between_jobs_check_time_increament;
	char *		jobs_trace_file; /* location of file with job traces */
	uint32_t    sdiag_period;
	char *		sdiag_file_out; /* location of file for sdiag output */
	uint32_t    sprio_period;
	char *      sprio_file_out; /* location of file for sprio output */
	uint32_t    sinfo_period;
	char *      sinfo_file_out; /* location of file for sinfo output */
	uint32_t    squeue_period;
	char *      squeue_file_out; /* location of file for squeue output */
	char *      sim_stat; /*sim stat output*/
	double      bf_model_real_prefactor;
	double      bf_model_real_power;
	double      bf_model_sim_prefactor;
	double      bf_model_sim_power;
	double      speed_scaling;
	char *      run_id;
	/* shared memory name, used to sync slurmdbd and slurmctrld, should be
	 * different if multiple simulation is running at same time */
	char *      shared_memory_name;
} slurm_sim_conf_t;

/* simulator configuration */
extern slurm_sim_conf_t *slurm_sim_conf;

/* read simulator configuration file */
extern int sim_read_sim_conf(void);

/* print simulator configuration */
extern int sim_print_sim_conf(void);

/******************************************************************************
 * Inter-process shared memory
 ******************************************************************************/

/* shared memory for syncronizing different processes */
#define SIM_SHM_SEGMENT_SIZE         72

/* Offsets */
//#define SIM_SECONDS_OFFSET            0
#define SIM_MICROSECONDS_OFFSET       0
#define SIM_SIM_MGR_PID_OFFSET       16
#define SIM_SLURMCTLD_PID_OFFSET     24
#define SIM_SLURMD_COUNT_OFFSET      32
#define SIM_SLURMD_REGISTERED_OFFSET 40
#define SIM_GLOBAL_SYNC_FLAG_OFFSET  48
#define SIM_SLURMD_PID_OFFSET        56
#define SIM_NEXT_SLURMD_EVENT_OFFSET 64
#define SIM_JOBS_DONE                68


extern void         * timemgr_data;
extern uint64_t     * sim_utime;
extern pid_t        * sim_mgr_pid;
extern pid_t        * slurmctl_pid;
extern int          * slurmd_count;
extern int          * slurmd_registered;
extern int          * global_sync_flag;
extern pid_t        * slurmd_pid;
extern uint32_t     * next_slurmd_event;
extern uint32_t     * sim_jobs_done;



extern char    syn_sem_name[];
extern sem_t * mutexserver;

extern char    sig_sem_name[];
extern sem_t * mutexsignal;

/******************************************************************************
 * Simulator job traces
 ******************************************************************************/
typedef struct job_trace {
    int  job_id;
    char *username;
    long int submit; /* relative or absolute? */
    int  duration;
    int  wclimit;
    int  tasks;
    char *qosname;
    char *partition;
    char *account;
    int  cpus_per_task;
    int  tasks_per_node;
    char *reservation;
    char *dependency;
    uint64_t pn_min_memory;/* minimum real memory (in MB) per node OR
     * real memory per CPU | MEM_PER_CPU,
     * NO_VAL use partition default,
     * default=0 (no limit) */
    char *features;
    char *gres;
    int shared;/* 2 if the job can only share nodes with other
     *   jobs owned by that user,
     * 1 if job can share nodes with other jobs,
     * 0 if job needs exclusive access to the node,
     * or NO_VAL to accept the system default.
     * SHARED_FORCE to eliminate user control. */
    long int cancelled; /* time when job should be cancelled, 0 if never */
    struct job_trace *next;
} job_trace_t;

extern job_trace_t *trace_head;
extern job_trace_t *trace_tail;

extern int sim_read_job_trace(const char*  workload_trace_file);

/* linked list of jobs curently in queue, to keep some of variable needed for
 * simulation
 */
extern job_trace_t *in_queue_trace_head;
extern job_trace_t *in_queue_trace_tail;

/* insert job to in_queue_trace */
extern int insert_in_queue_trace_record(job_trace_t *new);

/* insert job to in_queue_trace */
extern int remove_from_in_queue_trace_record(job_trace_t *remove);

/* find job in_queue_trace */
extern job_trace_t* find_job__in_queue_trace_record(int job_id);
/******************************************************************************
 * Simulator Events
 ******************************************************************************/
/*typedef enum simulator_event_type {
	SIM_JOB_AND_EPILOG_COMPLETE,
	SIM_JOB_COMPLETE,
	SIM_EPILOG_COMPLETE,
	SIM_CANCEL_JOB
} simulator_event_type_t;*/

typedef struct simulator_event {
    uint32_t job_id;
    int type;
    time_t when;
    uint32_t nodes_num;/*number of nodes in this job*/
    char *nodelist;
    char **nodenames;/*array with nodes names*/
    uint32_t *cores_used_on_node;
    time_t start_time;
    time_t last_update;
    double performance_factor;
    double work_total;
    double work_complete;
    struct simulator_event *next;
} simulator_event_t;

extern simulator_event_t *head_simulator_event;


typedef struct batch_job_launch_msg batch_job_launch_msg_t;

extern simulator_event_t * sim_alloc_simulator_event();
extern void sim_free_simulator_event(simulator_event_t *event);

extern int sim_add_future_event(batch_job_launch_msg_t *req);

extern simulator_event_t *head_simulator_cancel_event;
extern int sim_add_future_cancel_event(job_trace_t *trace);

/******************************************************************************
 * Operation on simulated time
 ******************************************************************************/

extern void sim_resume_clock();
extern void sim_pause_clock();
extern void sim_incr_clock(int seconds);
extern void sim_scale_clock(uint64_t start_sim_utime,double scale);
extern void sim_set_time(time_t unix_time);
extern unsigned int sim_sleep (unsigned int __seconds);
extern uint64_t get_sim_utime();
extern double get_realtime();
extern uint64_t get_real_utime();
extern void sim_set_new_time(uint64_t new_sim_utime);

extern void sim_backfill_step_scale(uint64_t start_sim_utime,uint64_t start_real_utime,int n);
extern void sim_backfill_scale(uint64_t start_sim_utime,uint64_t start_real_utime,int n);

/******************************************************************************
 * Calls to actual function which was substitute for simulation
 ******************************************************************************/

/******************************************************************************
 * Some simulation utils
 ******************************************************************************/

typedef struct sim_user_info{
    uid_t sim_uid;
    gid_t sim_gid;
    char *sim_name;
    struct sim_user_info *next;
}sim_user_info_t;

/* get uid from name */
extern uid_t sim_getuid(const char *name);
extern sim_user_info_t *get_sim_user_by_name(const char *name);


extern int (*sim_backfill_agent_ref)(void);

extern int (*sim_db_inx_handler_call_once)();

extern int sim_ctrl;


#endif
#endif
