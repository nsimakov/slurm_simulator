#ifndef _SIM_H
#define _SIM_H

#include <semaphore.h>
#include "slurm/slurm.h"

/******************************************************************************
 * Simulator Configuration Parameters
 ******************************************************************************/

/* Slurm simulator configuration parameters */
typedef struct slurm_sim_conf {
	uint32_t	time_start;	/* initial starting time will be overwritten by time from first job */
	uint32_t	time_stop;	/* final time when simulation should stop, 0-nether stop, 1-stop after all jobs are done*/
	uint32_t    seconds_before_first_job;
	double      clock_scaling;
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

#endif
