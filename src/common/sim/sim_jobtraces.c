#include "config.h"

#ifdef SLURM_SIMULATOR

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/log.h"
#include "src/common/sim/sim.h"

job_trace_t *trace_head=NULL;
job_trace_t *trace_tail=NULL;

static int insert_trace_record(job_trace_t *new)
{
	if (trace_head == NULL) {
		trace_head = new;
		trace_tail = new;
	} else {
		trace_tail->next = new;
		trace_tail = new;
	}
	return 0;
}

extern int sim_read_job_trace(const char*  workload_trace_file)
{
	struct stat  stat_buf;
	int          nrecs = 0, idx = 0;
	job_trace_t* job_arr;
	int trace_file;

	trace_file = open(workload_trace_file, O_RDONLY);
	if (trace_file < 0) {
		error("SIM: Error opening file %s", workload_trace_file);
		exit(1);
		return -1;
	}

	fstat(trace_file, &stat_buf);
	nrecs = stat_buf.st_size / sizeof(job_trace_t);
	info("SIM: Ci dev'essere %d job records to be read.", nrecs);

	job_arr = (job_trace_t*)malloc(sizeof(job_trace_t)*nrecs);
	if (!job_arr) {
		printf("SIM: Error.  Unable to allocate memory for all job records.\n");
		return -1;
	}

	while (read(trace_file, &job_arr[idx], sizeof(job_trace_t))) {
		job_arr[idx].next = NULL;
		insert_trace_record(&job_arr[idx]);
		++idx;
	}

	info("SIM: Trace initialization done. Total trace records: %d", idx);

	close(trace_file);
	/*free (job_arr);*/

	return 0;
}


#endif
