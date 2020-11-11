#include "slurm/slurm.h"
#include "src/common/log.h"
#include "src/common/sim/sim.h"
/*
 * "Constructor" function to be called before the main of each Slurm
 * entity (e.g. slurmctld, slurmd and commands).
 */

void __attribute__ ((constructor)) sim_init(void)
{
	info("Sim: Slurm simulator init.");
	sim_read_sim_conf();
	sim_print_sim_conf();
}
