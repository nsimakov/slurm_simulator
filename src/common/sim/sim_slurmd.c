#define main slurmd_main

#include "src/slurmd/slurmd/slurmd.c"

int slurmd_argc;
char **slurmd_argv;

static int _set_slurmd_arg(int argc, char **argv)
{
	/* argc and argv are from slurmctld update them */
	int i;
	char *slash;

	slurmd_argc = argc;
	slurmd_argv = xcalloc(slurmd_argc, sizeof(*slurmd_argv));

	for(i=0;i<argc;++i){
		xstrcat(slurmd_argv[i],argv[i]);
	}
	if ((slash = strrchr(slurmd_argv[0], '/')))
		slash[1] = '\0';
	else
		slurmd_argv[0] = '\0';
	xstrcat(slurmd_argv[0], "slurmd");

	return 0;
}

extern int sim_init_slurmd(int argc, char **argv)
{
	/*
	 * Create and set default values for the slurmd global
	 * config variable "conf"
	 */
	conf = xmalloc(sizeof(slurmd_conf_t));
	_init_conf();
	conf->daemonize   =  0;

	_set_slurmd_arg(argc, argv);
	conf->argv = &slurmd_argv;
	conf->argc = &slurmd_argc;

	if (_slurmd_init() < 0) {
		error("slurmd initialization failed");
		fflush( NULL);
		exit(1);
	}

	// rewind getopt
	optind = 1;

	return 0;
}

extern int sim_registration_engine()
{
	DEF_TIMERS;
	START_TIMER;
	while (1) {
		END_TIMER;
		if ((send_registration_msg(SLURM_SUCCESS, true) !=	 SLURM_SUCCESS)) {
			debug("Unable to register with slurm controller, retrying");
		} else if(DELTA_TIMER > 60000000) {
			error("Unable to register with slurm controller");
			exit(1);
		} else {
			break;
		}
		sleep(1);
	}
	return 0;
}
