#include "slurm/slurm.h"

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/log.h"

#include "src/common/sim/sim.h"

//typedef struct sim_event {
//	int64_t when; /* time of event in usec*/
//	struct sim_event *next;
//	struct sim_event *previous;
//	int type; /* event type */
//	void *payload; /* event type */
//
//} sim_event_t;

int64_t simulator_start_time=0;

pthread_mutex_t events_mutex = PTHREAD_MUTEX_INITIALIZER;

sim_event_t * sim_first_event = NULL;
sim_event_t * sim_last_event = NULL;
sim_event_t * sim_next_event = NULL;


void sim_insert_event2(sim_event_t * event)
{
	pthread_mutex_lock(&events_mutex);
	sim_event_t * following_event=sim_next_event;
	while(following_event->when < event->when) {
		following_event = following_event->next;
	}

	event->previous = following_event->previous;
	event->next = following_event;
	event->previous->next = event;
	following_event->previous = event;

	if(event->when < sim_next_event->when) {
		sim_next_event = event;
	}
	pthread_mutex_unlock(&events_mutex);
}


void sim_insert_event(int64_t when, int type, void *payload)
{
	sim_event_t * event = xcalloc(1,sizeof(*event));
	event->when = when;
	event->type = type;
	event->payload = payload;

	sim_insert_event2(event);
}

extern void sim_print_event(sim_event_t * event)
{
	int i;
	char *str=NULL;
	sim_event_submit_batch_job_t *payload=NULL;

	switch(event->type) {
	case SIM_NODE_REGISTRATION:
		info("%" PRId64 "\t SIM_NODE_REGISTRATION", event->when);
		break;
	case SIM_SUBMIT_BATCH_JOB:
		payload = (sim_event_submit_batch_job_t*)event->payload;
		for(i=0;i<payload->argc;++i) {
			xstrcat(str, payload->argv[i]);
			xstrcat(str, " ");
		}
		info("%" PRId64 "\tSIM_SUBMIT_BATCH_JOB --jid %d --sim-walltime %d %s",
				event->when, payload->job_id, payload->walltime, str);
		str[0]='\0';
		break;
	default:
		info("%" PRId64 "\t%d", event->when, event->type);
		break;
	}
	xfree(str);
}
extern void sim_print_events()
{
	info("Simulation Events:");
	sim_event_t * event=sim_first_event;
	while(event != NULL) {
		sim_print_event(event);
		event = event->next;
	}
	info("End Simulation Events:");
}

extern void split_cmd_line(const char * cmd_line, char ***argv, int *argc)
{
	int cmd_line_len = strlen(cmd_line);
	int m_argc = 0;
	char **m_argv=NULL;
	char *m_cmd_line;

	bool in_arg_mode=true;
	int i,m_i;

	m_cmd_line = xmalloc(cmd_line_len*sizeof(char));
	strncpy(m_cmd_line, cmd_line, cmd_line_len);

	// first pass: count args,
	// make m_cmd_line where all whitespaces are combined and replaced  with \0
	// and " is handled too

	i = 0;
	while(cmd_line[i]==' ' || cmd_line[i]=='\t') {
		++i;
	}
	m_i = 0;
	for (; i < cmd_line_len; ++i) {
		if(!in_arg_mode && (cmd_line[i]==' ' || cmd_line[i]=='\t')) {
			while((cmd_line[i]==' ' || cmd_line[i]=='\t') && i<cmd_line_len) {
				++i;
			}
			in_arg_mode = true;
			m_cmd_line[m_i]='\0';
			++m_i;
		}
		if(in_arg_mode) {
			while(cmd_line[i]!=' ' && cmd_line[i]!='\t' && i<cmd_line_len) {
				// handle escape
				if(i<cmd_line_len && cmd_line[i]=='"') {
					++i;
					while(cmd_line[i]!='"' && i<cmd_line_len) {
						if(cmd_line[i]!='\n'){
							m_cmd_line[m_i] = cmd_line[i];
							++m_i;
						}
						++i;
					}
				} else {
					if(cmd_line[i]!='\n'){
						m_cmd_line[m_i] = cmd_line[i];
						++m_i;
					}
				}
				++i;
			}
			in_arg_mode=false;
			++m_argc;
			--i;
		}
	}
	m_cmd_line[m_i]='\0';
	int m_cmd_line_len = m_i;

	// second pass set argv
	m_argv = (char**)xmalloc(m_argc*sizeof(char**));

	in_arg_mode = true;
	m_argv[0]=m_cmd_line;
	m_argc = 1;
	for (m_i = 0; m_i < m_cmd_line_len-1; ++m_i) {
		if(m_cmd_line[m_i]=='\0') {
			m_argv[m_argc]=m_cmd_line+m_i+1;
			++m_argc;
		}
	}

	*argc = m_argc;
	*argv = m_argv;
}

void* sim_submit_batch_job_get_payload(char *event_details)
{
	sim_event_submit_batch_job_t *payload = xcalloc(1,sizeof(sim_event_submit_batch_job_t));

	int iarg, argc;
	char **argv;

	split_cmd_line(event_details,&argv,&argc);

	payload->argv = xcalloc(argc + 1, sizeof(char*));
	payload->argv[0] = xstrdup("sbatch");
	payload->argc = 1;
	payload->walltime = -1;


	for(iarg=0;iarg<argc;++iarg){
		if(xstrcmp(argv[iarg], "-jid")==0 && iarg+1<argc){
			++iarg;
			payload->job_id = atoi(argv[iarg]);
		} else if(xstrcmp(argv[iarg], "-sim-walltime")==0 && iarg+1<argc){
			++iarg;
			payload->walltime = atoi(argv[iarg]);
		} else {
			payload->argv[payload->argc] = xstrdup(argv[iarg]);
			payload->argc += 1;
		}
	}
	if(payload->walltime < 0) {
		// i.e. run till walltime limit
		payload->walltime = INT32_MAX;
	}

	xfree(argv[0]);
	xfree(argv);
	return payload;
}

int sim_insert_event_by_cmdline(char *cmdline) {
	char * event_command = strtok(cmdline, "|");
	char * event_details = strtok(NULL, "|");

	int event_argc;
	char **event_argv;
	uint64_t start_time=0;
	int dt = -1;

	sim_event_t * event = xcalloc(1,sizeof(sim_event_t));
	event->type=0;

	// parse event type/when

	split_cmd_line(event_command,&event_argv,&event_argc);

	for(int iarg=0;iarg<event_argc;++iarg) {
		if(xstrcmp(event_argv[iarg], "-dt")==0 && iarg+1<event_argc){
			++iarg;
			dt = atoi(event_argv[iarg]);
		}
		if(xstrcmp(event_argv[iarg], "-e")==0 && iarg+1<event_argc){
			++iarg;
			if(xstrcmp(event_argv[iarg], "submit_batch_job")==0) {
				event->type = SIM_SUBMIT_BATCH_JOB;
			}
		}
	}

	xfree(event_argv[0]);
	xfree(event_argv);

	if(start_time==0 && dt == -1) {
		error("Start time is not set for %s (set either -t or -dt)", cmdline);
		return -1;
	}
	if(start_time!=0 && dt != -1) {
		error("Incorrect start time for %s (set either -t or -dt)", cmdline);
		return -1;
	}
	if(event->type == 0) {
		error("Unknown event type for %s (set either -t or -dt)", cmdline);
		return -1;
	}

	if(dt != -1) {
		event->when = simulator_start_time + dt * 1000000;
	}


	// parse event details
	if(event->type == SIM_SUBMIT_BATCH_JOB) {
		event->payload = sim_submit_batch_job_get_payload(event_details);
	}

	//xfree(event_details);
	//xfree(event_command);
	sim_insert_event2(event);
	return 0;
}

void sim_init_events()
{
	simulator_start_time = get_sim_utime();
	// pad events list with small and large time to avoid extra comparison
	sim_first_event=xcalloc(1,sizeof(*sim_first_event));
	sim_first_event->when = 0;
	sim_last_event=xcalloc(1,sizeof(*sim_last_event));
	sim_last_event->when = INT64_MAX;

	sim_first_event->next = sim_last_event;
	sim_last_event->previous = sim_first_event;

	sim_next_event = sim_first_event;

	// add first node registation
	sim_event_t * event = xcalloc(1,sizeof(sim_event_t));
	event->type=SIM_NODE_REGISTRATION;
	event->when=1;
	sim_insert_event2(event);

	// read events from simulation events file
	FILE *f_in = fopen(slurm_sim_conf->events_file, "rt");
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	if (f_in == NULL) {
		error("Can not open events file %s!", slurm_sim_conf->events_file);
		exit(1);
	}

	while ((read = getline(&line, &len, f_in)) != -1) {
		sim_insert_event_by_cmdline(line);
	}
	fclose(f_in);
}

void sim_insert_event_comp_job(uint32_t job_id)
{
	sim_job_t *active_job = sim_find_active_sim_job(job_id);
	if(active_job==NULL) {
		error("Sim: Can not find job %d among active sim jobs!", job_id);
		return;
	}
	int64_t when;
	if(active_job->start_time == 0){
		pthread_mutex_lock(&active_job_mutex);
		active_job->start_time = get_sim_utime();
		pthread_mutex_unlock(&active_job_mutex);
	}

	if(active_job->walltime != INT32_MAX){
		when = active_job->start_time + active_job->walltime * 1000000;
		sim_insert_event(when, SIM_COMPLETE_BATCH_SCRIPT, (void*)active_job);
	}
}
