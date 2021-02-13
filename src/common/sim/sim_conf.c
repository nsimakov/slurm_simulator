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
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurmdb_defs.h"

#include "sim/sim_funcs.h"


slurm_sim_conf_t *slurm_sim_conf=NULL;


extern int sim_read_sim_conf(void)
{
	s_p_options_t options[] = {
		{"TimeStart", S_P_UINT32},
		{"StartSecondsBeforeFirstJob", S_P_LONG},
		{"TimeStop", S_P_UINT32},
		{"TimeStep", S_P_DOUBLE},
		{"AfterJobLaunchTimeIncreament", S_P_UINT32},
		{"BFBetweenJobsChecksTimeIncreament", S_P_UINT32},
		{"JobsTraceFile", S_P_STRING},
		{"sdiagPeriod", S_P_UINT32},
		{"sdiagFileOut", S_P_STRING},
		{"sprioPeriod", S_P_UINT32},
		{"sprioFileOut", S_P_STRING},
		{"sinfoPeriod", S_P_UINT32},
		{"sinfoFileOut", S_P_STRING},
		{"squeuePeriod", S_P_UINT32},
		{"squeueFileOut", S_P_STRING},
		{"SimStats", S_P_STRING},
		{"RunID", S_P_STRING},
		{"SpeedScale", S_P_DOUBLE},
		{"bf_model_real_prefactor", S_P_DOUBLE},
		{"bf_model_real_power", S_P_DOUBLE},
		{"bf_model_sim_prefactor", S_P_DOUBLE},
		{"bf_model_sim_power", S_P_DOUBLE},
		{"SharedMemoryName", S_P_STRING},
		{NULL} };
	s_p_hashtbl_t *tbl = NULL;
	char *conf_path = NULL;
	struct stat buf;
	double time_step;

	/* Set initial values */
	if (slurm_sim_conf == NULL) {
		slurm_sim_conf = xmalloc(sizeof(slurm_sim_conf_t));
	}
	slurm_sim_conf->time_start=978325200;
	slurm_sim_conf->start_seconds_before_first_job=30;
	slurm_sim_conf->time_stop=1;
	slurm_sim_conf->time_step=1000000;
	slurm_sim_conf->after_job_launch_time_increament=0;
	slurm_sim_conf->bf_between_jobs_check_time_increament=0;

	slurm_sim_conf->sdiag_period=0;
	slurm_sim_conf->sdiag_file_out=NULL;
	slurm_sim_conf->sprio_period=0;
	slurm_sim_conf->sprio_file_out=NULL;
	slurm_sim_conf->sinfo_period=0;
	slurm_sim_conf->sinfo_file_out=NULL;
	slurm_sim_conf->squeue_period=0;
	slurm_sim_conf->squeue_file_out=NULL;
	slurm_sim_conf->run_id=NULL;

	slurm_sim_conf->speed_scaling=1.0;
	slurm_sim_conf->bf_model_real_prefactor=1.0;
	slurm_sim_conf->bf_model_real_power=1.0;
	slurm_sim_conf->bf_model_sim_prefactor=1.0;
	slurm_sim_conf->bf_model_sim_power=1.0;

	slurm_sim_conf->shared_memory_name=NULL;

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

		if (!s_p_get_string(&slurm_sim_conf->sdiag_file_out, "sdiagFileOut", tbl))
			slurm_sim_conf->sdiag_file_out = NULL;

		if (!s_p_get_string(&slurm_sim_conf->sim_stat, "SimStats", tbl))
			slurm_sim_conf->sim_stat = NULL;

		s_p_get_uint32(&slurm_sim_conf->time_start, "TimeStart", tbl);
		s_p_get_long(&slurm_sim_conf->start_seconds_before_first_job, "StartSecondsBeforeFirstJob", tbl);
		s_p_get_uint32(&slurm_sim_conf->time_stop, "TimeStop", tbl);

		if(s_p_get_double(&time_step, "TimeStep", tbl)){
			if(time_step>600)
				fatal("SIM: TimeStep is too big (%f). Should be 1 second or less",time_step);
			slurm_sim_conf->time_step=(uint32_t)(time_step*1000000.0);
			info("slurm_sim_conf->time_step=%d us",slurm_sim_conf->time_step);
		}
		s_p_get_uint32(&slurm_sim_conf->after_job_launch_time_increament, "AfterJobLaunchTimeIncreament", tbl);
		s_p_get_uint32(&slurm_sim_conf->bf_between_jobs_check_time_increament, "BFBetweenJobsChecksTimeIncreament", tbl);

		s_p_get_uint32(&slurm_sim_conf->sdiag_period, "sdiagPeriod", tbl);
		s_p_get_string(&slurm_sim_conf->sdiag_file_out, "sdiagFileOut", tbl);
		s_p_get_uint32(&slurm_sim_conf->sprio_period, "sprioPeriod", tbl);
		s_p_get_string(&slurm_sim_conf->sprio_file_out, "sprioFileOut", tbl);
		s_p_get_uint32(&slurm_sim_conf->sinfo_period, "sinfoPeriod", tbl);
		s_p_get_string(&slurm_sim_conf->sinfo_file_out, "sinfoFileOut", tbl);
		s_p_get_uint32(&slurm_sim_conf->squeue_period, "squeuePeriod", tbl);
		s_p_get_string(&slurm_sim_conf->squeue_file_out, "squeueFileOut", tbl);

		if(!s_p_get_string(&slurm_sim_conf->shared_memory_name, "SharedMemoryName", tbl))
			slurm_sim_conf->shared_memory_name=xstrdup("/slurm_sim.shm");

		s_p_get_string(&slurm_sim_conf->run_id,"RunID",tbl);

		int scaling_factor_is_set=s_p_get_double(&slurm_sim_conf->speed_scaling, "SpeedScale", tbl);
		int bf_model_scaling_is_set=0;
		bf_model_scaling_is_set+=s_p_get_double(&slurm_sim_conf->bf_model_real_prefactor, "bf_model_real_prefactor", tbl);
		bf_model_scaling_is_set+=s_p_get_double(&slurm_sim_conf->bf_model_real_power, "bf_model_real_power", tbl);
		bf_model_scaling_is_set+=s_p_get_double(&slurm_sim_conf->bf_model_sim_prefactor, "bf_model_sim_prefactor", tbl);
		bf_model_scaling_is_set+=s_p_get_double(&slurm_sim_conf->bf_model_sim_power, "bf_model_sim_power", tbl);

#ifdef BFMODEL_POW
		if(bf_model_scaling_is_set>0){
			fatal("This slurm simulator compiled for use with SpeedScale "
				  "To use backfill power model for time scaling recompile with BFMODEL_POW");
		}
#else
		if(scaling_factor_is_set>0 && bf_model_scaling_is_set>0){
			fatal("SpeedScale is set in sim.conf as well as "
				  "bf_model_real_prefactor, bf_model_real_power, bf_model_sim_prefactor and bf_model_sim_power are set."
		          "This two parameters set are exclusive!");
		}
#endif
		if(scaling_factor_is_set>0){
			slurm_sim_conf->bf_model_real_prefactor=slurm_sim_conf->speed_scaling;
			slurm_sim_conf->bf_model_real_power=1.0;
			slurm_sim_conf->bf_model_sim_prefactor=1.0;
			slurm_sim_conf->bf_model_sim_power=1.0;
		}else{
			slurm_sim_conf->speed_scaling=-1.0;
		}

		s_p_hashtbl_destroy(tbl);
	}

	xfree(conf_path);

	return SLURM_SUCCESS;
}
extern int sim_print_sim_conf(void)
{
	if(slurm_sim_conf->jobs_trace_file!=NULL)
		info("JobsTraceFile=%s",slurm_sim_conf->jobs_trace_file);
	else
		info("JobsTraceFile=(null)");

	if(slurm_sim_conf->sdiag_file_out!=NULL)
		info("sdiagFileOut=%s",slurm_sim_conf->sdiag_file_out);
	else
		info("sdiagFileOut=(null)");


	if(slurm_sim_conf->sim_stat!=NULL)
		info("SimStats=%s",slurm_sim_conf->sim_stat);
	else
		info("SimStats=(null)");

	info("TimeStart=%zu",slurm_sim_conf->time_start);
	if(slurm_sim_conf->time_start==0)
		info("    i.e. Slurm Simulator spins forever");
	if(slurm_sim_conf->time_start==1)
		info("    i.e. Slurm Simulator stops after last job is done.");

	info("StartSecondsBeforeFirstJob=%ld",slurm_sim_conf->start_seconds_before_first_job);
	info("TimeStop=%zu",slurm_sim_conf->time_stop);
	info("TimeStep=%zu usec",slurm_sim_conf->time_step);
	info("AfterJobLaunchTimeIncreament=%zu",slurm_sim_conf->after_job_launch_time_increament);
	info("BFBetweenJobsChecksTimeIncreament=%zu",slurm_sim_conf->bf_between_jobs_check_time_increament);

	info("sdiagPeriod=%zu",slurm_sim_conf->sdiag_period);
	if(slurm_sim_conf->sdiag_file_out!=NULL)
		info("sdiagFileOut=%s",slurm_sim_conf->sdiag_file_out);
	else
		info("sdiagFileOut=(null)");

	info("sprioPeriod=%zu",slurm_sim_conf->sprio_period);
	if(slurm_sim_conf->sprio_file_out!=NULL)
		info("sprioFileOut=%s",slurm_sim_conf->sprio_file_out);
	else
		info("sprioFileOut=(null)");

	info("sinfoPeriod=%zu",slurm_sim_conf->sinfo_period);
	if(slurm_sim_conf->sinfo_file_out!=NULL)
		info("sinfoFileOut=%s",slurm_sim_conf->sinfo_file_out);
	else
		info("sinfoFileOut=(null)");

	info("squeuePeriod=%zu",slurm_sim_conf->squeue_period);
	if(slurm_sim_conf->squeue_file_out!=NULL)
		info("squeueFileOut=%s",slurm_sim_conf->squeue_file_out);
	else
		info("squeueFileOut=(null)");

	if(slurm_sim_conf->shared_memory_name!=NULL)
		info("SharedMemoryName=%s",slurm_sim_conf->shared_memory_name);
	else
		info("SharedMemoryName=(null)");

	if(slurm_sim_conf->run_id!=NULL)
		info("RunID=%s",slurm_sim_conf->run_id);
	else
		info("RunID=(null)");

	if(slurm_sim_conf->speed_scaling>0.0){
		info("scalingFactor=%f",slurm_sim_conf->speed_scaling);
	}else{
		info("bf_model_real_prefactor=%f",slurm_sim_conf->bf_model_real_prefactor);
		info("bf_model_real_power=%f",slurm_sim_conf->bf_model_real_power);
		info("bf_model_sim_prefactor=%f",slurm_sim_conf->bf_model_sim_prefactor);
		info("bf_model_sim_power=%f",slurm_sim_conf->bf_model_sim_power);
	}
	return SLURM_SUCCESS;
}
#endif
