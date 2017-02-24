
#ifdef SLURM_SIMULATOR

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



/* Shared Memory */
void         * timemgr_data = NULL;
uint32_t     * current_sim = NULL;
uint32_t     * current_micro = NULL;
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

/* Function Prototypes */
static void init_funcs();
void init_shared_memory_if_needed();
int getting_simulation_users();

static int clock_ticking=0;
static struct timeval ref_timeval={0,0};
static struct timeval prev_sim_timeval={0,0};

time_t time(time_t *t)
{
	init_shared_memory_if_needed();
	/* If the current_sim pointer is NULL that means that there is no
	 * shared memory segment, at least not yet, therefore use real function
	 * for now.
	 * Note, here we are examing the location of to where the pointer points
	 *       and not the value itself.
	 */
	if (!(current_sim) && !real_time) init_funcs();
	if (!(current_sim)) {
		return real_time(t);
	}

	if(clock_ticking){
		//i.e. clock ticking but time is shifted
		struct timeval cur_real_time;
		real_gettimeofday(&cur_real_time,NULL);

		*(current_sim)=prev_sim_timeval.tv_sec+cur_real_time.tv_sec-ref_timeval.tv_sec;
		*(current_micro)=prev_sim_timeval.tv_usec+cur_real_time.tv_usec-ref_timeval.tv_usec;
		if(*(current_micro)>1000000){
			*(current_sim)+=1;
			*(current_micro)-=1000000;
		}
	}

	return *(current_sim);
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	init_shared_memory_if_needed();
	if (!(current_sim)) {
		if (attaching_shared_memory() < 0) {
			error("SIM: Error attaching/building shared memory "
			      "and mmaping it");
		};
		if (!real_gettimeofday) init_funcs();
		return real_gettimeofday(tv, tz);
	}

	if(clock_ticking){
		//i.e. clock ticking but time is shifted
		struct timeval cur_real_time;
		real_gettimeofday(&cur_real_time,NULL);

		*(current_sim)=prev_sim_timeval.tv_sec+cur_real_time.tv_sec-ref_timeval.tv_sec;
		*(current_micro)=prev_sim_timeval.tv_usec+cur_real_time.tv_usec-ref_timeval.tv_usec;
		if(*(current_micro)>1000000){
			*(current_sim)+=1;
			*(current_micro)-=1000000;
		}
	}else{
		//i.e. clock not ticking
		*(current_micro) = *(current_micro) + 10;
	}

	tv->tv_sec       = *(current_sim);
	tv->tv_usec      = *(current_micro);

	return 0;
}

extern void sim_resume_clock()
{
	prev_sim_timeval.tv_sec=*current_sim;
	prev_sim_timeval.tv_usec=*current_micro;

	clock_ticking=1;

	real_gettimeofday(&ref_timeval,NULL);


}
extern void sim_pause_clock()
{
	gettimeofday(&prev_sim_timeval,NULL);

	clock_ticking=0;
}

extern void sim_incr_clock(int seconds)
{
	if(clock_ticking==0)
		*current_sim=*current_sim+seconds;
}
extern void sim_set_time(time_t unix_time)
{
	real_gettimeofday(&ref_timeval,NULL);

	*current_sim=unix_time;
	*current_micro=ref_timeval.tv_usec;

	prev_sim_timeval.tv_sec=*current_sim;
	prev_sim_timeval.tv_usec=*current_micro;
}
extern unsigned int sim_sleep (unsigned int __seconds)
{
	time_t sleep_till=time(NULL)+__seconds;
	while(sleep_till<time(NULL)){
		usleep(100);
	}
}

static int build_shared_memory()
{
	int fd;

	fd = shm_open(SLURM_SIM_SHM, O_CREAT | O_RDWR,
				S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0) {
		int err = errno;
		error("SIM: Error opening %s -- %s", SLURM_SIM_SHM,strerror(err));
		return -1;
	}

	if (ftruncate(fd, SIM_SHM_SEGMENT_SIZE)) {
		info("SIM: Warning!  Can not truncate shared memory segment.");
	}

	timemgr_data = mmap(0, SIM_SHM_SEGMENT_SIZE, PROT_READ | PROT_WRITE,
							MAP_SHARED, fd, 0);

	if(!timemgr_data){
		debug("SIM: mmaping %s file can not be done\n", SLURM_SIM_SHM);
		return -1;
	}

	return 0;

}





/*
 * Slurmctld and slurmd do not really build shared memory but they use that
 * one built by sim_mgr
 */
extern int attaching_shared_memory()
{
	int fd;

	fd = shm_open(SLURM_SIM_SHM, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO );
	if (fd >= 0) {
		if (ftruncate(fd, SIM_SHM_SEGMENT_SIZE)) {
			info("SIM: Warning! Can't truncate shared memory segment.");
		}
		timemgr_data = mmap(0, SIM_SHM_SEGMENT_SIZE,
				    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	} else {
		build_shared_memory();
	}

	if (!timemgr_data) {
		error("SIM: mmaping %s file can not be done", SLURM_SIM_SHM);
		return -1;
	}

	/* Initializing pointers to shared memory */
	current_sim       = timemgr_data + SIM_SECONDS_OFFSET;
	current_micro     = timemgr_data + SIM_MICROSECONDS_OFFSET;
	sim_mgr_pid       = timemgr_data + SIM_SIM_MGR_PID_OFFSET;
	slurmctl_pid      = timemgr_data + SIM_SLURMCTLD_PID_OFFSET;
	slurmd_count      = timemgr_data + SIM_SLURMD_COUNT_OFFSET;
	slurmd_registered = timemgr_data + SIM_SLURMD_REGISTERED_OFFSET;
	global_sync_flag  = timemgr_data + SIM_GLOBAL_SYNC_FLAG_OFFSET;
	slurmd_pid        = timemgr_data + SIM_SLURMD_PID_OFFSET;
	next_slurmd_event = timemgr_data + SIM_NEXT_SLURMD_EVENT_OFFSET;
	sim_jobs_done     = timemgr_data + SIM_JOBS_DONE;

	return 0;
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
}

void init_shared_memory_if_needed()
{
	if (!(current_sim)) {
		if (attaching_shared_memory() < 0) {
			error("SIM: Error attaching/building shared memory "
			      "and mmaping it");
		};
	}
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
	char username[100], users_sim_file_name[128];
	char uid_string[10];

	int rv = 0;

	if (sim_users_list)
		return 0;

	determine_users_sim_path();
	sprintf(users_sim_file_name, "%s%s", users_sim_path, "users.sim");
	FILE *fin=fopen(users_sim_file_name,"rt");
	if (fin ==NULL) {
		info("ERROR: SIM: no users.sim available");
		return -1;
	}

	debug("SIM: Starting reading users...");

	char *line = NULL;
	ssize_t read;
	size_t i,len = 0;

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
			goto finish;
		}
		debug("Reading user %s", username);
		new_sim_user->sim_name = xstrdup(username);
		new_sim_user->sim_uid = (uid_t)tmp_uid;
		new_sim_user->sim_gid = (gid_t)tmp_gid;

		// Inserting as list head
		new_sim_user->next = sim_users_list;
		sim_users_list = new_sim_user;

	}
finish:
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

	sim_read_sim_conf();

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
	static uint32_t oldtime = 0;

	while (*global_sync_flag < 2 || *current_sim < oldtime + 1) {
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
	oldtime = *current_sim;
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
	int sec=usec/1000000;
	uint32_t oldtime = *current_sim;

	while(*current_sim-oldtime<sec){
		usleep(10);
	}
}

#endif
