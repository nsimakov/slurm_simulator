
#ifdef SLURM_SIMULATOR

#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include "sim/sim_funcs.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
extern errno;

/* Structures, macros and other definitions */
#undef DEBUG
//#define DEBUG




/* Function Pointers */
int (*real_gettimeofday)(struct timeval *,struct timezone *) = NULL;
time_t (*real_time)(time_t *)                                = NULL;
unsigned int (*real_sleep)(unsigned int seconds)              = NULL;
int (*real_usleep)(useconds_t usec)                           = NULL;

/* Shared Memory */
void         * timemgr_data = NULL;
uint64_t     * sim_utime = NULL;
pid_t        * sim_mgr_pid = NULL;
pid_t        * slurmctl_pid = NULL;
int          * slurmd_count = NULL;
int          * slurmd_registered = NULL;
int          * global_sync_flag = NULL;
pid_t        * slurmd_pid = NULL;
uint32_t     * next_slurmd_event = NULL;
uint32_t     * sim_jobs_done = NULL;

/* Global Variables */

sim_user_info_t * sim_users_list;


char            * users_sim_path = NULL;
char            * lib_loc;
char            * libc_paths[4] = {"/lib/x86_64-linux-gnu/libc.so.6",
				   "/lib/libc.so.6","/lib64/libc.so.6",
				   NULL};


extern char     * default_slurm_config_file;

/* reference to sched_plugin */
int (*sim_backfill_agent_ref)(void)=NULL;

int (*sim_db_inx_handler_call_once)()=NULL;

int sim_ctrl=0;

/* name of shared memory segment */
char * sim_shared_memory_name=NULL;

/* Function Prototypes */
static void init_funcs();
int getting_simulation_users();

static int clock_ticking=0;
static uint64_t ref_utime=0;
static uint64_t prev_sim_utime=0;

time_t time(time_t *t)
{
	if(clock_ticking){
		//i.e. clock ticking but time is shifted
		struct timeval cur_real_time;
		uint64_t cur_real_utime;
		real_gettimeofday(&cur_real_time,NULL);
		cur_real_utime=cur_real_time.tv_sec*1000000+cur_real_time.tv_usec;

		*sim_utime=cur_real_utime-ref_utime+prev_sim_utime;

	}
	return *(sim_utime)/1000000;
}
double get_realtime()
{
	struct timeval cur_real_time;
	uint64_t cur_real_utime;
	real_gettimeofday(&cur_real_time,NULL);
	cur_real_utime=cur_real_time.tv_sec*1000000+cur_real_time.tv_usec;

	double t=cur_real_utime*0.000001;

	return t;
}
uint64_t get_real_utime()
{
	struct timeval cur_real_time;
	uint64_t cur_real_utime;
	real_gettimeofday(&cur_real_time,NULL);
	cur_real_utime=cur_real_time.tv_sec*1000000+cur_real_time.tv_usec;
	return cur_real_utime;
}
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	if(clock_ticking){
		//i.e. clock ticking but time is shifted
		struct timeval cur_real_time;
		uint64_t cur_real_utime;
		real_gettimeofday(&cur_real_time,NULL);
		cur_real_utime=cur_real_time.tv_sec*1000000+cur_real_time.tv_usec;

		*sim_utime=cur_real_utime-ref_utime+prev_sim_utime;
	}else{
		//i.e. clock not ticking
		*(sim_utime) = *(sim_utime) + 10;
	}

	tv->tv_sec       = *(sim_utime)/1000000;
	tv->tv_usec      = *(sim_utime)%1000000;

	return 0;
}
uint64_t get_sim_utime()
{
	if(clock_ticking){
		//i.e. clock ticking but time is shifted
		struct timeval cur_real_time;
		uint64_t cur_real_utime;
		real_gettimeofday(&cur_real_time,NULL);
		cur_real_utime=cur_real_time.tv_sec*1000000+cur_real_time.tv_usec;

		*sim_utime=cur_real_utime-ref_utime+prev_sim_utime;
	}
	return *sim_utime;
}
extern void sim_resume_clock()
{
	struct timeval ref_timeval;
	prev_sim_utime=*sim_utime;

	clock_ticking=1;

	real_gettimeofday(&ref_timeval,NULL);

	ref_utime=ref_timeval.tv_sec*1000000+ref_timeval.tv_usec;
}
extern void sim_pause_clock()
{
	struct timeval prev_sim_timeval;
	gettimeofday(&prev_sim_timeval,NULL);
	prev_sim_utime=prev_sim_timeval.tv_sec*1000000+prev_sim_timeval.tv_usec;

	clock_ticking=0;
}

extern void sim_incr_clock(int microseconds)
{
	if(clock_ticking==0)
		*sim_utime=*sim_utime+microseconds;
}
extern void sim_scale_clock(uint64_t start_sim_utime,double scale)
{
	if(clock_ticking){
		uint64_t cur_sim_utime=get_sim_utime();
		uint64_t cur_dutime=cur_sim_utime-start_sim_utime;
		uint64_t new_dutime=(uint64_t)((double)cur_dutime*scale);

		ref_utime=ref_utime-(new_dutime-cur_dutime);
	}
}
extern void sim_set_new_time(uint64_t new_sim_utime)
{
	if(clock_ticking){
		uint64_t cur_sim_utime=get_sim_utime();
		int64_t dutime=new_sim_utime-cur_sim_utime;

		ref_utime=ref_utime-dutime;
	}else{
		*sim_utime=new_sim_utime;
	}
}

#ifdef BFMODEL_POW

double bf_model(double prefactor,double power,double n)
{
	return prefactor*pow(n,power);
}
double bf_step_model(double prefactor,double power,double n)
{
	return prefactor*(pow(n,power)-pow(n-1,power));
}
extern void sim_backfill_step_scale(uint64_t start_sim_utime,uint64_t start_real_utime,int n)
{
	if(n==0)
		return;
	uint64_t cur_real_utime=get_real_utime();
	uint64_t cur_sim_utime=get_sim_utime();

	double real_dt=cur_real_utime-start_real_utime;

	double scale=bf_step_model(slurm_sim_conf->bf_model_real_prefactor,slurm_sim_conf->bf_model_real_power,(double)n)/
			     bf_step_model(slurm_sim_conf->bf_model_sim_prefactor,slurm_sim_conf->bf_model_sim_power,(double)n);

	double sim_dt=scale*real_dt;

	uint64_t new_sim_utime=start_sim_utime+(int)round(sim_dt);

	if(new_sim_utime>cur_sim_utime)
		sim_set_new_time(new_sim_utime);
}


extern void sim_backfill_scale(uint64_t start_sim_utime,uint64_t start_real_utime,int n)
{
	if(n==0)
		return;
	uint64_t cur_real_utime=get_real_utime();
	uint64_t cur_sim_utime=get_sim_utime();

	double real_dt=cur_real_utime-start_real_utime;

	double scale=bf_model(slurm_sim_conf->bf_model_real_prefactor,slurm_sim_conf->bf_model_real_power,(double)n)/
			     bf_model(slurm_sim_conf->bf_model_sim_prefactor,slurm_sim_conf->bf_model_sim_power,(double)n);

	double sim_dt=scale*real_dt;

	uint64_t new_sim_utime=start_sim_utime+(int)round(sim_dt);

	if(new_sim_utime>cur_sim_utime)
		sim_set_new_time(new_sim_utime);
}
#else
extern void sim_backfill_step_scale(uint64_t start_sim_utime,uint64_t start_real_utime,int n)
{
	uint64_t cur_real_utime=get_real_utime();
	uint64_t cur_sim_utime=get_sim_utime();

	double real_dt=cur_real_utime-start_real_utime;
	double sim_dt=slurm_sim_conf->speed_scaling*real_dt;

	uint64_t new_sim_utime=start_sim_utime+(int)round(sim_dt);

	if(new_sim_utime>cur_sim_utime)
		sim_set_new_time(new_sim_utime);
}

extern void sim_backfill_scale(uint64_t start_sim_utime,uint64_t start_real_utime,int n)
{
	uint64_t cur_real_utime=get_real_utime();
	uint64_t cur_sim_utime=get_sim_utime();

	double real_dt=cur_real_utime-start_real_utime;
	double sim_dt=slurm_sim_conf->speed_scaling*real_dt;

	uint64_t new_sim_utime=start_sim_utime+(int)round(sim_dt);

	if(new_sim_utime>cur_sim_utime)
		sim_set_new_time(new_sim_utime);
}
#endif
extern void sim_set_time(time_t unix_time)
{
	struct timeval ref_timeval;
	real_gettimeofday(&ref_timeval,NULL);

	*sim_utime=unix_time*1000000;
	*sim_utime+=ref_timeval.tv_usec;

	prev_sim_utime=*sim_utime;
}
/*extern unsigned int sim_sleep (unsigned int __seconds)
{
	time_t sleep_till=time(NULL)+__seconds;
	while(sleep_till<time(NULL)){
		usleep(100);
	}
}*/

/* get and build shared memory name if needed */
char * get_shared_memory_name()
{
	if(sim_shared_memory_name==NULL){
		sim_shared_memory_name=(char*)xmalloc(256*sizeof(char));
		if(slurm_sim_conf==NULL)
			sim_shared_memory_name=xstrdup("/slurm_sim.shm");
		else if(slurm_sim_conf->shared_memory_name==NULL)
			sim_shared_memory_name=xstrdup("/slurm_sim.shm");
		else
			sim_shared_memory_name=xstrdup(slurm_sim_conf->shared_memory_name);

	}

	return sim_shared_memory_name;
}

static int build_shared_memory()
{
	int fd;

	fd = shm_open(get_shared_memory_name(), O_CREAT | O_RDWR,
				S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0) {
		int err = errno;
		error("SIM: Error opening %s -- %s", get_shared_memory_name(),strerror(err));
		return -1;
	}

	if (ftruncate(fd, SIM_SHM_SEGMENT_SIZE)) {
		info("SIM: Warning!  Can not truncate shared memory segment.");
	}

	timemgr_data = mmap(0, SIM_SHM_SEGMENT_SIZE, PROT_READ | PROT_WRITE,
							MAP_SHARED, fd, 0);

	if(!timemgr_data){
		debug("SIM: mmaping %s file can not be done\n", get_shared_memory_name());
		return -1;
	}

	return 0;

}





/*
 * slurmd build shared memory (because it run first) and
 * Slurmctld attached to it
 */
extern int attaching_shared_memory()
{
	int fd;
	int new_shared_memory=0;

	fd = shm_open(get_shared_memory_name(), O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO );
	if (fd >= 0) {
		if (ftruncate(fd, SIM_SHM_SEGMENT_SIZE)) {
			info("SIM: Warning! Can't truncate shared memory segment.");
		}
		timemgr_data = mmap(0, SIM_SHM_SEGMENT_SIZE,
				    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	} else {
		build_shared_memory();
		new_shared_memory=1;
	}

	if (!timemgr_data) {
		error("SIM: mmaping %s file can not be done", get_shared_memory_name());
		return -1;
	}

	/* Initializing pointers to shared memory */
	sim_utime         = timemgr_data + SIM_MICROSECONDS_OFFSET;
	sim_mgr_pid       = timemgr_data + SIM_SIM_MGR_PID_OFFSET;
	slurmctl_pid      = timemgr_data + SIM_SLURMCTLD_PID_OFFSET;
	slurmd_count      = timemgr_data + SIM_SLURMD_COUNT_OFFSET;
	slurmd_registered = timemgr_data + SIM_SLURMD_REGISTERED_OFFSET;
	global_sync_flag  = timemgr_data + SIM_GLOBAL_SYNC_FLAG_OFFSET;
	slurmd_pid        = timemgr_data + SIM_SLURMD_PID_OFFSET;
	next_slurmd_event = timemgr_data + SIM_NEXT_SLURMD_EVENT_OFFSET;
	sim_jobs_done     = timemgr_data + SIM_JOBS_DONE;

	return new_shared_memory;
}

static void
determine_libc() {
	struct stat buf;
	int ix;
	char found = 0;

	libc_paths[3] = getenv("SIM_LIBC_PATH");

	for (ix=2; ix>=0 && !found; --ix) {
		lib_loc = libc_paths[ix];
		if (lib_loc) {
			if (!stat(lib_loc, &buf)) ++found;
		}
	}

	if (!found) {
		error("SIM: Could not find the libc file."
		      "  Try setting SIM_LIBC_PATH.");
	}
}

static void init_funcs()
{
	void* handle;

	if (real_gettimeofday == NULL) {
		debug("SIM: Looking for real gettimeofday function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			debug("SIM: Error in dlopen %s", dlerror());
			return;
		}

		real_gettimeofday = dlsym( handle, "gettimeofday");
		if (real_gettimeofday == NULL) {
			error("Error:SIM:  no sleep function found");
			return;
		}
	}

	if (real_time == NULL) {
		debug("SIM: Looking for real time function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("SIM: Error in dlopen: %s", dlerror());
			return;
		}
		real_time = dlsym( handle, "time");
		if (real_time == NULL) {
			error("SIM: Error: no sleep function found");
			return;
		}
	}
	if (real_sleep == NULL) {
		debug("SIM: Looking for real sleep function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("SIM: Error in dlopen: %s", dlerror());
			return;
		}
		real_sleep = dlsym( handle, "sleep");
		if (real_sleep == NULL) {
			error("SIM: Error: no sleep function found");
			return;
		}
	}
	if (real_usleep == NULL) {
		debug("SIM: Looking for real sleep function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("SIM: Error in dlopen: %s", dlerror());
			return;
		}
		real_usleep = dlsym( handle, "usleep");
		if (real_usleep == NULL) {
			error("SIM: Error: no sleep function found");
			return;
		}
	}

	//unsigned int (*real_sleep(unsigned int seconds);             = NULL;
	//int (*real_usleep(useconds_t usec)                           = NULL;
}

/* User- and uid-related functions */
uid_t sim_getuid(const char *name)
{
	sim_user_info_t *aux;

	if (!sim_users_list) getting_simulation_users();

	aux = sim_users_list;
	debug2("SIM: sim_getuid: starting search for username %s", name);

	while (aux) {
		if (strcmp(aux->sim_name, name) == 0) {
			debug2("SIM: sim_getuid: found uid %u for username %s",
						aux->sim_uid, aux->sim_name);
			debug2("SIM: sim_getuid--name: %s uid: %u",
						name, aux->sim_uid);
			return aux->sim_uid;
		}
		aux = aux->next;
	}

	debug2("SIM: sim_getuid--name: %s uid: <Can NOT find uid>", name);
	return -1;
}

sim_user_info_t *get_sim_user(uid_t uid)
{
	sim_user_info_t *aux;

	aux = sim_users_list;

	while (aux) {
		if (aux->sim_uid == uid) {
			return aux;
		}
		aux = aux->next;
	}

	return NULL;
}

sim_user_info_t *get_sim_user_by_name(const char *name)
{
	sim_user_info_t *aux;

	aux = sim_users_list;

	while (aux) {
		if (strcmp(aux->sim_name, name) == 0) {
			return aux;
		}
		aux = aux->next;
	}

	return NULL;
}


char *sim_getname(uid_t uid)
{
	sim_user_info_t *aux;
	char *user_name;

	aux = sim_users_list;

	while (aux) {
		if (aux->sim_uid == uid) {
			user_name = xstrdup(aux->sim_name);
			return user_name;
		}
		aux = aux->next;
	}

	return NULL;
}

int getpwnam_r(const char *name, struct passwd *pwd, 
		char *buf, size_t buflen, struct passwd **result)
{

	pwd->pw_uid = sim_getuid(name);
	if (pwd->pw_uid == -1) {
		*result = NULL;
		debug("SIM: No user found for name %s", name);
		return ENOENT;
	}
	pwd->pw_name = xstrdup(name);
	debug("SIM: Found uid %u for name %s", pwd->pw_uid, pwd->pw_name);

	*result = pwd;

	return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pwd,
		char *buf, size_t buflen, struct passwd **result)
{
	sim_user_info_t *sim_user=get_sim_user(uid);

	if (sim_user == NULL) {
		*result = NULL;
		debug("SIM: No user found for uid %u", uid);
		return ENOENT;
	}

	pwd->pw_uid = uid;
	pwd->pw_name = xstrdup(sim_user->sim_name);
	pwd->pw_gid = sim_user->sim_gid;

	*result = pwd;

	debug("SIM: Found name %s for uid %u and gid %u", pwd->pw_name, pwd->pw_uid, pwd->pw_gid);
	return 0;

}

void determine_users_sim_path()
{
	char *ptr = NULL;

	if (!users_sim_path) {
		char *name = getenv("SLURM_CONF");
		if (name) {
			users_sim_path = xstrdup(name);
		} else {
			users_sim_path = xstrdup(default_slurm_config_file);
		}

		ptr = strrchr(users_sim_path, '/');
		if (ptr) {
			/* Found a path, truncate the file name */
			++ptr;
			*ptr = '\0';
		} else {
			xfree(users_sim_path);
			users_sim_path = xstrdup("./");
		}
	}
}

int getting_simulation_users()
{
	char username[128], users_sim_file_name[512];
	char uid_string[10];
	char stmp[128];

	int rv = 0;

	if (sim_users_list)
		return 0;

	FILE *fin;
	char *line;
	ssize_t read;
	size_t len;

    //read real users from /etc/passwd
	fin=fopen("/etc/passwd","rt");
	if (fin ==NULL) {
		info("ERROR: SIM: can not open /etc/passwd");
		return -1;
	}
	//printf("Reading users\n");

	debug("SIM: Starting reading real users from /etc/passwd");

	line = NULL;
	len = 0;

	while ((read = getline(&line, &len, fin)) != -1) {
		size_t i;
		int tmp_uid,tmp_gid;
		for(i=0;i<len;++i)
			if(line[i]==':')
				line[i]=' ';

		read=sscanf(line,"%s x %d %d",username,&tmp_uid,&tmp_gid);
		if(read==2)
			tmp_gid=100;

		if(read<2){
			info("ERROR: SIM: unknown format of /etc/passwd for %s",line);
			continue;
		}

		sim_user_info_t * new_sim_user = xmalloc(sizeof(sim_user_info_t));
		if (new_sim_user == NULL) {
			error("SIM: Malloc error for new sim user");
			rv = -1;
			break;
		}
		debug("Reading user %s", username);
		//printf("%s %d %d\n",username,tmp_uid,tmp_gid);
		new_sim_user->sim_name = xstrdup(username);
		new_sim_user->sim_uid = (uid_t)tmp_uid;
		new_sim_user->sim_gid = (gid_t)tmp_gid;

		// Inserting as list head
		new_sim_user->next = sim_users_list;
		sim_users_list = new_sim_user;

	}
	free(line);
	fclose(fin);
	//read simulated users
	determine_users_sim_path();
	sprintf(users_sim_file_name, "%s%s", users_sim_path, "users.sim");
	fin=fopen(users_sim_file_name,"rt");
	if (fin ==NULL) {
		info("ERROR: SIM: no users.sim available");
		return -1;
	}

	debug("SIM: Starting reading users...");

	line=NULL;
	len = 0;

	while ((read = getline(&line, &len, fin)) != -1) {
		size_t i;
		int tmp_uid,tmp_gid;
		for(i=0;i<len;++i)
			if(line[i]==':')
				line[i]=' ';

		read=sscanf(line,"%s %d %d",username,&tmp_uid,&tmp_gid);
		if(read==2)
			tmp_gid=100;

		if(read<2){
			info("ERROR: SIM: unknown format of users.sim for %s",line);
			continue;
		}

		sim_user_info_t * new_sim_user = xmalloc(sizeof(sim_user_info_t));
		if (new_sim_user == NULL) {
			error("SIM: Malloc error for new sim user");
			rv = -1;
			break;
		}
		debug("Reading user %s", username);
		new_sim_user->sim_name = xstrdup(username);
		new_sim_user->sim_uid = (uid_t)tmp_uid;
		new_sim_user->sim_gid = (gid_t)tmp_gid;

		//printf("%s %d %d\n",username,tmp_uid,tmp_gid);

		// Inserting as list head
		new_sim_user->next = sim_users_list;
		sim_users_list = new_sim_user;

	}
	free(line);
	fclose(fin);
	return rv;
}

void
free_simulation_users()
{
	sim_user_info_t * sim_user = sim_users_list;

	while (sim_user) {
		sim_users_list = sim_user->next;
	
		/* deleting the list head */
		xfree(sim_user->sim_name);
		xfree(sim_user);

		sim_user = sim_users_list;
	}
	sim_users_list = NULL;
}

/*
 * "Constructor" function to be called before the main of each Slurm
 * entity (e.g. slurmctld, slurmd and commands).
 */

void __attribute__ ((constructor)) sim_init(void)
{
	void *handle;
#ifdef DEBUG
	sim_user_info_t *debug_list;
#endif
	determine_libc();

	sim_read_sim_conf();

	if (attaching_shared_memory() < 0) {
		error("Error attaching/building shared memory and mmaping it");
	};


	if (getting_simulation_users() < 0) {
		error("Error getting users information for simulation");
	}

#ifdef DEBUG
	debug_list = sim_users_list;
	while (debug_list) {
		info("User %s with uid %u", debug_list->sim_name,
					debug_list->sim_uid);
		debug_list = debug_list->next;
	}
#endif

	if (real_gettimeofday == NULL) {
		debug("Looking for real gettimeofday function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("Error in dlopen %s", dlerror());
			return;
		}

		real_gettimeofday = dlsym( handle, "gettimeofday");
		if (real_gettimeofday == NULL) {
			error("Error: no sleep function found");
			return;
		}
	}

	if (real_time == NULL) {
		debug("Looking for real time function");

		handle = dlopen(lib_loc, RTLD_LOCAL | RTLD_LAZY);
		if (handle == NULL) {
			error("Error in dlopen: %s", dlerror());
			return;
		}
		real_time = dlsym( handle, "time");
		if (real_time == NULL) {
			error("Error: no sleep function found\n");
			return;
		}
	}


	sim_pause_clock();
	sim_set_new_time(slurm_sim_conf->time_start*(uint64_t)1000000);
	sim_resume_clock();

	debug("sim_init: done");
}

int
sim_open_sem(char * sem_name, sem_t ** mutex_sync, int max_attempts)
{
	int iter = 0, max_iter = max_attempts;
	if (!max_iter) max_iter = 10;
	while ((*mutex_sync) == SEM_FAILED && iter < max_iter) {
		(*mutex_sync) = sem_open(sem_name, 0, 0755, 0);
		if ((*mutex_sync) == SEM_FAILED && max_iter > 1) {
			int err = errno;
			info("ERROR! Could not open semaphore (%s)-- %s",
					sem_name, strerror(err));
			sleep(1);
		}
		++iter;
	}

	if ((*mutex_sync) == SEM_FAILED)
		return -1;
	else
		return 0;
}

void
sim_perform_global_sync(char * sem_name, sem_t ** mutex_sync)
{
	static uint64_t old_utime = 0;

	while (*global_sync_flag < 2 || *sim_utime < old_utime + 1) {
		usleep(100000); /* one-tenth second */
	}

	if (*mutex_sync != SEM_FAILED) {
		sem_wait(*mutex_sync);
	} else {
		while ( *mutex_sync == SEM_FAILED ) {
			sim_open_sem(sem_name, mutex_sync, 0);
		}
		sem_wait(*mutex_sync);
	}

	*global_sync_flag += 1;
	if (*global_sync_flag > *slurmd_count + 1)
		*global_sync_flag = 1;
	old_utime = *sim_utime;
	sem_post(*mutex_sync);
}

void
sim_perform_slurmd_register(char * sem_name, sem_t ** mutex_sync)
{
	if (*mutex_sync != SEM_FAILED) {
		sem_wait(*mutex_sync);
	} else {
		while ( *mutex_sync == SEM_FAILED ) {
			sim_open_sem(sem_name, mutex_sync, 0);
		}
		sem_wait(*mutex_sync);
	}

	*slurmd_registered += 1;
	sem_post(*mutex_sync);
}

void
sim_close_sem(sem_t ** mutex_sync)
{
	if ((*mutex_sync) != SEM_FAILED) {
		sem_close((*mutex_sync));
	}
}

void
sim_usleep(int usec)
{
	uint64_t old_utime = *sim_utime;

	while(*sim_utime-old_utime<usec){
		usleep(10);
	}
}


#endif
