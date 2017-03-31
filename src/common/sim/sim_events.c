#include "config.h"

#ifdef SLURM_SIMULATOR

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <slurm/slurm_errno.h>
#include <src/common/forward.h>
#include <src/common/hostlist.h>
#include <src/common/node_select.h>
#include <src/common/parse_time.h>
#include <src/common/slurm_accounting_storage.h>
#include <src/common/slurm_jobcomp.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/switch.h>
#include <src/common/xassert.h>
#include <src/common/xstring.h>
#include <src/common/assoc_mgr.h>
#include "sim.h"

simulator_event_t *head_simulator_event=NULL;

void _sim_insert_event(simulator_event_t *new_event)
{
	if (!head_simulator_event) {
		debug3("SIM: Adding new event for job %d when list is empty "
		     "for future time %ld!",
		     new_event->job_id, new_event->when);
		head_simulator_event = new_event;
	} else {
		simulator_event_t *node_temp = head_simulator_event;
		debug3("SIM: Adding new event for job %d in the event listi "
		     "for future time %ld", new_event->job_id, new_event->when);

		if(head_simulator_event->when > new_event->when){
			new_event->next = head_simulator_event;
			head_simulator_event = new_event;
			return;
		}

		while ((node_temp->next) &&
				(node_temp->next->when < new_event->when))
			node_temp = node_temp->next;

		if(node_temp->next){
			new_event->next = node_temp->next;
			node_temp->next = new_event;
			return;
		}
		node_temp->next = new_event;
	}
}

extern void sim_free_simulator_event(simulator_event_t *event){
	int i;

	if(event->nodelist!=NULL){
		xfree(event->nodelist);
	}
	if(event->nodenames!=NULL){
		for(i=0;i<event->nodes_num;++i)
			xfree(event->nodenames[i]);
		xfree(event->nodenames);
	}

	xfree(event);
}

extern simulator_event_t * sim_alloc_simulator_event()
{
	simulator_event_t *new_event = (simulator_event_t *)malloc(sizeof(simulator_event_t));
	if(!new_event){
		error("SIMULATOR: malloc fails for new_event\n");
		//pthread_mutex_unlock(&simulator_mutex);
		return -1;
	}
	memset(new_event,0,sizeof(simulator_event_t));
	return new_event;
}

extern int sim_add_future_event(batch_job_launch_msg_t *req)
{
	int32_t inode,i_cpu_group,i_node_in_cpu_group,ijob;
	simulator_event_t  *new_event;
	time_t now;

	struct node_record *node_ptr;

	//pthread_mutex_lock(&simulator_mutex);
	now = time(NULL);

	new_event = sim_alloc_simulator_event();
	job_trace_t *trace=find_job__in_queue_trace_record(req->job_id);

	assert(trace);

	new_event->job_id = req->job_id;
	if(trace->cancelled > 0){
                //if it starts before cancelling let it run for 0 sec
		new_event->type = REQUEST_COMPLETE_BATCH_SCRIPT;
		new_event->when = now;
	} else {
		new_event->type = REQUEST_COMPLETE_BATCH_SCRIPT;
		new_event->when = now + trace->duration;
	}
	new_event->next = NULL;

	_sim_insert_event(new_event);

	return 0;
}


simulator_event_t *head_simulator_cancel_event=NULL;
void _sim_insert_cancel_event(simulator_event_t *new_event)
{
	if (!head_simulator_cancel_event) {
		debug3("SIM: Adding new cancel_event for job %d when list is empty "
		     "for future time %ld!",
		     new_event->job_id, new_event->when);
		head_simulator_cancel_event = new_event;
	} else {
		simulator_event_t *node_temp = head_simulator_cancel_event;
		debug3("SIM: Adding new event for job %d in the event listi "
		     "for future time %ld", new_event->job_id, new_event->when);

		if(head_simulator_cancel_event->when > new_event->when){
			new_event->next = head_simulator_cancel_event;
			head_simulator_cancel_event = new_event;
			return;
		}

		while ((node_temp->next) &&
				(node_temp->next->when < new_event->when))
			node_temp = node_temp->next;

		if(node_temp->next){
			new_event->next = node_temp->next;
			node_temp->next = new_event;
			return;
		}
		node_temp->next = new_event;
	}
}

extern int sim_add_future_cancel_event(job_trace_t *trace)
{
	simulator_event_t  *new_event;
	new_event = sim_alloc_simulator_event();

	new_event->job_id = trace->job_id;
	new_event->type = REQUEST_CANCEL_JOB;
	new_event->when = trace->cancelled;
	new_event->next = NULL;

	_sim_insert_cancel_event(new_event);

	return 0;
}
#endif
