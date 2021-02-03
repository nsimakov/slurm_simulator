#include "config.h"

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurmdb_defs.h"

#include "sim/sim.h"

slurm_sim_conf_t *slurm_sim_conf = NULL;

extern int sim_read_sim_conf(void) {
	s_p_options_t options[] = {
			{"TimeStart", S_P_UINT32 },
			{"TimeStop", S_P_UINT32 },
			{"SecondsBeforeFirstJob", S_P_DOUBLE },
			{"ClockScaling", S_P_DOUBLE },
			{"SharedMemoryName", S_P_STRING },
			{"EventsFile", S_P_STRING },
			{"TimeAfterAllEventsDone", S_P_LONG },
			{ NULL } };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	struct stat buf;
	double seconds_before_first_job;

	/* Set initial values */
	if (slurm_sim_conf == NULL) {
		slurm_sim_conf = xmalloc(sizeof(slurm_sim_conf_t));
	}
	slurm_sim_conf->time_start = 978325200;
	slurm_sim_conf->time_stop = 0;
	slurm_sim_conf->microseconds_before_first_job = 30000000;

	slurm_sim_conf->shared_memory_name = NULL;
	slurm_sim_conf->events_file = NULL;


	/* Get the slurmdbd.conf path and validate the file */
	conf_path = get_extra_conf_path("sim.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		info("SIM: No sim.conf file (%s)", conf_path);
	} else {
		debug("SIM: Reading sim.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false) == SLURM_ERROR) {
			fatal("SIM: Could not open/read/parse sim.conf file %s", conf_path);
		}

		s_p_get_uint32(&slurm_sim_conf->time_start, "TimeStart", tbl);
		s_p_get_uint32(&slurm_sim_conf->time_stop, "TimeStop", tbl);
		if (s_p_get_double(&seconds_before_first_job, "SecondsBeforeFirstJob", tbl)) {
			slurm_sim_conf->microseconds_before_first_job = (uint64_t)(seconds_before_first_job*1.0e6);
		}
		s_p_get_double(&slurm_sim_conf->clock_scaling, "ClockScaling", tbl);

		if (!s_p_get_string(&slurm_sim_conf->shared_memory_name,
				"SharedMemoryName", tbl)) {
			slurm_sim_conf->shared_memory_name = xstrdup("/slurm_sim.shm");
		}

		if (!s_p_get_string(&slurm_sim_conf->events_file, "EventsFile", tbl)) {
			slurm_sim_conf->events_file = xstrdup("sim.events");
		}

		if (slurm_sim_conf->events_file[0] != '/') {
			slurm_sim_conf->events_file = get_extra_conf_path(
					slurm_sim_conf->events_file);
		}

		s_p_get_long(&slurm_sim_conf->time_after_all_events_done, "TimeAfterAllEventsDone", tbl);

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return SLURM_SUCCESS;
}

extern int sim_print_sim_conf(void) {
	info("Sim: Slurm simulator configuration:");
	info("TimeStart=%u", slurm_sim_conf->time_start);
	info("TimeStop=%u", slurm_sim_conf->time_stop);
	if (slurm_sim_conf->time_stop == 0)
		info("    i.e. Slurm Simulator spins forever");
	if (slurm_sim_conf->time_stop == 1)
		info("    i.e. Slurm Simulator stops after last job is done.");

	info("SecondsBeforeFirstJob=%f", slurm_sim_conf->microseconds_before_first_job/1.0e6);
	info("ClockScaling=%f", slurm_sim_conf->clock_scaling);

	if (slurm_sim_conf->shared_memory_name != NULL)
		info("SharedMemoryName=%s", slurm_sim_conf->shared_memory_name);
	else
		info("SharedMemoryName=(null)");
	if (slurm_sim_conf->events_file != NULL)
		info("EventsFile=%s", slurm_sim_conf->events_file);
	else
		info("EventsFile=(null)");

	info("TimeAfterAllEventsDone=%ld", slurm_sim_conf->time_after_all_events_done);
	return SLURM_SUCCESS;
}
