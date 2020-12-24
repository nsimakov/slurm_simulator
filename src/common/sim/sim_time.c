#include "src/common/log.h"

#include <stdlib.h>
#include <dlfcn.h>

#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>

#include <sys/time.h>

char *libc_loc;
char *libc_paths[5] = {
		"here will be SIM_LIBC_PATH env",
		"/lib64/libc.so.6",
		"/lib/libc.so.6",
		"/lib/x86_64-linux-gnu/libc.so.6",
		NULL};

extern int64_t *sim_timeval_shift;
extern double *sim_timeval_scale;

/* Function Pointers */
int (*real_gettimeofday)(struct timeval *,struct timezone *) = NULL;
time_t (*real_time)(time_t *) = NULL;
unsigned int (*real_sleep)(unsigned int seconds) = NULL;
int (*real_usleep)(useconds_t usec) = NULL;
int (*real_nanosleep)(const struct timespec *req, struct timespec *rem) = NULL;

//static uint64_t ref_utime=0;
//static uint64_t prev_sim_utime=0;
//
//time_t time(time_t *t)
//{
//	if(clock_ticking){
//		//i.e. clock ticking but time is shifted
//		struct timeval cur_real_time;
//		uint64_t cur_real_utime;
//		real_gettimeofday(&cur_real_time,NULL);
//		cur_real_utime=cur_real_time.tv_sec*1000000+cur_real_time.tv_usec;
//
//		*sim_utime=cur_real_utime-ref_utime+prev_sim_utime;
//
//	}
//	return *(sim_utime)/1000000;
//}
//

int64_t get_real_utime()
{
	struct timeval cur_real_time;
	real_gettimeofday(&cur_real_time, NULL);

	int64_t cur_real_utime = (int64_t) (cur_real_time.tv_sec) * (int64_t) 1000000 + (int64_t) (cur_real_time.tv_usec);
	return cur_real_utime;
}

int64_t get_sim_utime()
{
	int64_t cur_real_utime = get_real_utime();
	int64_t cur_sim_time = cur_real_utime + *sim_timeval_shift + (int64_t)((*sim_timeval_scale - 1.0)*cur_real_utime);
	return cur_sim_time;
}

void set_sim_time(int64_t cur_sim_time, double scale)
{
	struct timeval cur_real_time;
	real_gettimeofday(&cur_real_time, NULL);

	int64_t cur_real_utime = (int64_t) (cur_real_time.tv_sec) * (int64_t) 1000000 + (int64_t) (cur_real_time.tv_usec);

	*sim_timeval_scale = scale;
	// essentially cur_sim_time - (*sim_timeval_scale)*cur_real_utime
	// reformatted to avoid overflow
	*sim_timeval_shift = (int64_t)((1.0-*sim_timeval_scale)*cur_sim_time) -
			(int64_t)(*sim_timeval_scale * (cur_real_utime - cur_sim_time));

	debug2("sim_timeval_shift %ld sim_timeval_scale %f\n\n", *sim_timeval_shift, *sim_timeval_scale);
}

void set_sim_time_scale(double scale)
{
	if (scale != *sim_timeval_scale) {
		set_sim_time(get_sim_utime(), scale);
	}
}

int gettimeofday(struct timeval *tv, void *tz)
{
	int64_t cur_sim_time =  get_sim_utime();
	tv->tv_sec       = cur_sim_time/1000000;
	tv->tv_usec      = cur_sim_time%1000000;
	return 0;
}

time_t time(time_t *t)
{
	int64_t cur_sim_time =  get_sim_utime();
	time_t ts = cur_sim_time / 1000000;
	if (t != NULL) {
		*t = ts;
	}
	return ts;
}

unsigned int sleep (unsigned int seconds)
{
	//return real_sleep(seconds);
	int64_t sleep_till = get_sim_utime() + 1000000 * seconds;
	while(get_sim_utime() < sleep_till){
		real_usleep(1000);
	}
	return 0;
}

int usleep (useconds_t usec)
{
	return real_usleep(usec);
	useconds_t real_usec = 100;
	if (real_usec > usec) {
		real_usec = usec;
	};
	int64_t sleep_till = get_sim_utime() + usec;
	while(get_sim_utime() <= sleep_till){
		real_usleep(real_usec);
	}
	return 0;
}

int nanosleep (const struct timespec *req, struct timespec *rem)
{
	return real_nanosleep(req, rem);
	int64_t nanosec = req->tv_sec*1000000000+req->tv_nsec;
	int64_t usec = nanosec/1000;
	usleep(usec);
	if(rem!=NULL) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	return 0;
}

static void determine_libc()
{
	struct stat buf;
	int i;

	libc_paths[0] = getenv("SIM_LIBC_PATH");

	for (i = 0; !(i > 0 && libc_paths == NULL); ++i) {
		libc_loc = libc_paths[i];
		if (libc_loc) {
			if (!stat(libc_loc, &buf)) {
				return;
			}
		}
	}

	error("Sim: Could not find the libc file. Try setting SIM_LIBC_PATH.");
	exit(1);
}

static void *get_libc_func(const char *name)
{
	void *handle;
	debug("Sim: Looking for real %s function", name);

	handle = dlopen(libc_loc, RTLD_LOCAL | RTLD_LAZY);
	if (handle == NULL) {
		error("Sim: Error in dlopen %s", dlerror());
		exit(1);
	}

	void *func = dlsym(handle, name);
	if (func == NULL) {
		error("Sim:  no %s function found", name);
		exit(1);
	}
	return func;
}


static void set_pointers_to_time_func()
{
	if (real_gettimeofday == NULL) {
		real_gettimeofday = get_libc_func("gettimeofday");
	}
	if (real_time == NULL) {
		real_time = get_libc_func("time");
	}
	if (real_sleep == NULL) {
		real_sleep = get_libc_func("sleep");
	}
	if (real_usleep == NULL) {
		real_usleep = get_libc_func("usleep");
	}
	if (real_nanosleep == NULL) {
		real_nanosleep = get_libc_func("nanosleep");
	}
}


void init_sim_time(uint32_t start_time, double scale, int set_time, int set_time_to_real)
{
	int64_t cur_sim_time;

	determine_libc();
	set_pointers_to_time_func();

	if (set_time_to_real > 0 || start_time==0) {
		cur_sim_time = get_real_utime();
	} else {
		cur_sim_time = (int64_t) start_time * (int64_t) 1000000;
	}

	if (set_time > 0) {
		set_sim_time(cur_sim_time, scale);
	}
}
