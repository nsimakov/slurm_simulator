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

job_trace_t *in_queue_trace_head=NULL;
job_trace_t *in_queue_trace_tail=NULL;

extern int insert_in_queue_trace_record(job_trace_t *new)
{
	if (in_queue_trace_head == NULL) {
		in_queue_trace_head = new;
		in_queue_trace_tail = new;
	} else {
		in_queue_trace_tail->next = new;
		in_queue_trace_tail = new;
	}
	return 0;
}
extern int remove_from_in_queue_trace_record(job_trace_t *remove)
{
	if(remove==NULL){
		error("SIM: remove_from_in_queue_trace_record remove==NULL");
		return 1;
	}
	if (in_queue_trace_head==NULL && in_queue_trace_tail==NULL){
		error("SIM: remove_from_in_queue_trace_record no jobs in in_queue_trace");
		return 1;
	}

	job_trace_t *prev=NULL;
	job_trace_t *next=remove->next;
	remove->next=NULL;

	job_trace_t *trace=in_queue_trace_head;

	//if only trace
	if (in_queue_trace_head==remove && in_queue_trace_tail==remove) {
		in_queue_trace_head = NULL;
		in_queue_trace_tail = NULL;
		return 0;
	}
	//if head trace
	if (in_queue_trace_head == remove) {
		in_queue_trace_head = next;
		return 0;
	}

	//at the end or middle
	while(trace!=NULL){
		if(trace->next==remove){
			prev=trace;
			break;
		}
		trace=trace->next;
	}
	if(prev!=NULL){
		prev->next=next;
		if(in_queue_trace_tail==remove)
			in_queue_trace_tail=prev;
		return 0;
	}else{
		error("SIM: remove_from_in_queue_trace_record can not find previous element");
	}

	return 1;
}

/* find job in_queue_trace */
extern job_trace_t* find_job__in_queue_trace_record(int job_id)
{
	if(in_queue_trace_head==NULL)
		return NULL;

	job_trace_t *node_temp = in_queue_trace_head;
	while((node_temp!=NULL) && (node_temp->job_id != job_id))
		node_temp = node_temp->next;

	return node_temp;
}

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

#define read_single_var(v,f) fread(&v,sizeof(v),1,f);

int _read_string(char **ps, FILE *trace_file)
{
	char *s;
    int l;
    read_single_var(l,trace_file);
    s=(char*)calloc(l+1,sizeof(char));
    if(l>0)
    {
    	fread(s,sizeof(char),l,trace_file);
    }
    *ps=s;
    return 0;
}
#define read_string(v,f) _read_string(&(v),f)

#define SIM_NEW_MEM_PER_CPU  0x8000000000000000
#define SIM_OLD_MEM_PER_CPU  0x80000000

job_trace_t* read_single_trace(FILE *trace_file)
{
	job_trace_t *trace = (job_trace_t*)calloc(1,sizeof(job_trace_t));
	if (!trace) {
		printf("SIM: Error.  Unable to allocate memory for job record.\n");
		return -1;
	}
	read_single_var(trace->job_id,trace_file);
	read_string(trace->username, trace_file);

	read_single_var(trace->submit,trace_file);
	read_single_var(trace->duration,trace_file);
	read_single_var(trace->wclimit,trace_file);
	read_single_var(trace->tasks,trace_file);
	read_string(trace->qosname, trace_file);
	read_string(trace->partition, trace_file);
	read_string(trace->account, trace_file);
	read_single_var(trace->cpus_per_task,trace_file);
	read_single_var(trace->tasks_per_node,trace_file);
	read_string(trace->reservation, trace_file);
	read_string(trace->dependency, trace_file);

	read_single_var(trace->pn_min_memory,trace_file);
	/*
	uint64_t pn_min_memory;
	read_single_var(pn_min_memory,trace_file);
	if(pn_min_memory==NO_VAL64){
		trace->pn_min_memory=NO_VAL;
	}else{
		uint64_t req_mem=pn_min_memory & (~SIM_NEW_MEM_PER_CPU);
		uint64_t req_mem_per_cpu=pn_min_memory & SIM_NEW_MEM_PER_CPU;

		trace->pn_min_memory=(uint32_t)req_mem;
		if(req_mem_per_cpu){
			trace->pn_min_memory=trace->pn_min_memory|SIM_OLD_MEM_PER_CPU;
		}
	}*/

	read_string(trace->features, trace_file);
	read_string(trace->gres, trace_file);
	read_single_var(trace->shared,trace_file);
	read_single_var(trace->cancelled, trace_file);
	trace->next=NULL;
    return trace;
}


extern int sim_read_job_trace(const char*  workload_trace_file)
{
	int idx = 0;

	FILE *trace_file = fopen(workload_trace_file, "rb");
	if (trace_file == NULL) {
		error("SIM: Error opening file %s", workload_trace_file);
		exit(1);
		return -1;
	}

	job_trace_t *trace=NULL;
	while(feof(trace_file)==0)
	{
		trace=read_single_trace(trace_file);
		insert_trace_record(trace);
		++idx;
	}

	info("SIM: Trace initialization done. Total trace records: %d", idx);

	fclose(trace_file);

	return 0;
}


#endif
