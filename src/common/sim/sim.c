#include "slurm/slurm.h"
#include "src/common/log.h"
#include "src/common/sim/sim.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>


/* Offsets */
#define SIM_MICROSECONDS_OFFSET       0

/* Shared Memory */
void         * sim_shmem_data = NULL;
int64_t *sim_timeval_shift = NULL;
double *sim_timeval_scale = NULL;


extern void init_sim_time(uint32_t start_time, double scale);

static int shared_memory_size()
{
	return sizeof(*sim_timeval_shift) + sizeof(*sim_timeval_scale) + 16;
}


static int build_shared_memory()
{
	int fd;

	fd = shm_open(slurm_sim_conf->shared_memory_name, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0) {
		int err = errno;
		error("Sim: Error opening %s -- %s", slurm_sim_conf->shared_memory_name,strerror(err));
		return -1;
	}

	if (ftruncate(fd, shared_memory_size())) {
		info("Sim: Warning!  Can not truncate shared memory segment.");
	}

	sim_shmem_data = mmap(0, shared_memory_size(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if(!sim_shmem_data){
		debug("Sim: mapping %s file can not be done\n", slurm_sim_conf->shared_memory_name);
		return -1;
	}

	return 0;
}

/*
 * slurmd build shared memory (because it run first) and
 * Slurmctld attached to it
 */
extern int attach_shared_memory()
{
	int fd;
	int new_shared_memory=0;

	fd = shm_open(slurm_sim_conf->shared_memory_name, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO );
	if (fd >= 0) {
		if (ftruncate(fd, shared_memory_size())) {
			info("Sim: Warning! Can't truncate shared memory segment.");
		}
		sim_shmem_data = mmap(0, shared_memory_size(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	} else {
		build_shared_memory();
		new_shared_memory=1;
	}

	if (!sim_shmem_data) {
		error("Sim: mapping %s file can not be done", slurm_sim_conf->shared_memory_name);
		return -1;
	}

	/* Initializing pointers to shared memory */
	int offset = 0;
	sim_timeval_shift  = sim_shmem_data + offset;
	offset += sizeof(*sim_timeval_shift);
	sim_timeval_scale  = sim_shmem_data + offset;
	offset += sizeof(*sim_timeval_scale);

	return new_shared_memory;
}


/*
 * "Constructor" function to be called before the main of each Slurm
 * entity (e.g. slurmctld, slurmd and commands).
 */

void __attribute__ ((constructor)) sim_init(void)
{
	info("Sim: Slurm simulator init.");

	sim_read_sim_conf();
	sim_print_sim_conf();

	int new_shared_memory = attach_shared_memory();

	if (new_shared_memory < 0) {
		error("Error attaching/building shared memory and mmaping it");
		exit(1);
	};

	init_sim_time(new_shared_memory*slurm_sim_conf->time_start, slurm_sim_conf->clock_scaling);
}
