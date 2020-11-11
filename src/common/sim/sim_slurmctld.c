#define main slurmctld_main
#include "src/slurmctld/controller.c"
#undef main

#include "src/common/sim/sim.h"


extern slurmctld_config_t slurmctld_config;

extern int sim_controller();


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
		sim_controller();
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
	sim_registration_engine();


	while(1){
		sleep(1);
	}
	return 0;
}
