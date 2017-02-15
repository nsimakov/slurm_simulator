#include "config.h"
#ifdef SLURM_SIMULATOR

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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurmdb_defs.h"

#include "sim/sim_funcs.h"


slurm_sim_conf_t *slurm_sim_conf=NULL;


extern int sim_read_sim_conf(void)
{
	s_p_options_t options[] = {
		{"TimeStart", S_P_UINT32},
		{"TimeStop", S_P_UINT32},
		{"TimeStep", S_P_UINT32},
		{"JobsTraceFile", S_P_STRING},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	struct stat buf;

	/* Set initial values */
	if (slurm_sim_conf == NULL) {
		slurm_sim_conf = xmalloc(sizeof(slurm_sim_conf_t));
	}
	slurm_sim_conf->time_start=978325200;
	slurm_sim_conf->time_stop=978325200;
	slurm_sim_conf->time_step=978325200;

	/* Get the slurmdbd.conf path and validate the file */
	conf_path = get_extra_conf_path("sim.conf");
	if ((conf_path == NULL) || (stat(conf_path, &buf) == -1)) {
		info("SIM: No sim.conf file (%s)", conf_path);
	} else {
		debug("SIM: Reading sim.conf file %s", conf_path);

		tbl = s_p_hashtbl_create(options);
		if (s_p_parse_file(tbl, NULL, conf_path, false) == SLURM_ERROR) {
			fatal("SIM: Could not open/read/parse sim.conf file %s",
			      conf_path);
		}

		if (!s_p_get_string(&slurm_sim_conf->jobs_trace_file, "JobsTraceFile", tbl))
			slurm_sim_conf->jobs_trace_file = xstrdup("test.trace");

		s_p_get_uint32(&slurm_sim_conf->time_start, "TimeStart", tbl);
		s_p_get_uint32(&slurm_sim_conf->time_stop, "TimeStop", tbl);
		s_p_get_uint32(&slurm_sim_conf->time_step, "TimeStep", tbl);


		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return SLURM_SUCCESS;
}

#endif
