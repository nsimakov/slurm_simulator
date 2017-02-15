#ifndef _SLURM_SIM_H
#define _SLURM_SIM_H

#ifdef SLURM_SIMULATOR

#define SLURM_SIM_SHM "/tester_slurm_sim.shm"
#define SIM_SHM_SEGMENT_SIZE         72

/* Offsets */
#define SIM_SECONDS_OFFSET            0
#define SIM_MICROSECONDS_OFFSET       8
#define SIM_SIM_MGR_PID_OFFSET       16
#define SIM_SLURMCTLD_PID_OFFSET     24
#define SIM_SLURMD_COUNT_OFFSET      32
#define SIM_SLURMD_REGISTERED_OFFSET 40
#define SIM_GLOBAL_SYNC_FLAG_OFFSET  48
#define SIM_SLURMD_PID_OFFSET        56
#define SIM_NEXT_SLURMD_EVENT_OFFSET 64
#define SIM_JOBS_DONE                68

#include "slurm/slurm.h"

void         * timemgr_data;
uint32_t     * current_sim;
uint32_t     * current_micro;
pid_t        * sim_mgr_pid;
pid_t        * slurmctl_pid;
int          * slurmd_count;
int          * slurmd_registered;
int          * global_sync_flag;
pid_t        * slurmd_pid;
uint32_t     * next_slurmd_event;
uint32_t     * sim_jobs_done;

extern char    syn_sem_name[];
extern sem_t * mutexserver;

extern char    sig_sem_name[];
extern sem_t * mutexsignal;


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
    volatile struct simulator_event *next;
} simulator_event_t;


#endif
#endif
