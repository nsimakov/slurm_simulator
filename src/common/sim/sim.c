#include "slurm/slurm.h"
#include "src/common/log.h"
/*
 * "Constructor" function to be called before the main of each Slurm
 * entity (e.g. slurmctld, slurmd and commands).
 */

void __attribute__ ((constructor)) sim_init(void)
{
	info("Slurm simulator init.");
}
