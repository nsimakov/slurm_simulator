#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include <stdlib.h>
#include <dlfcn.h>

#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>

#include <sys/time.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>

#include <string.h>

#include <inttypes.h>

char *libc_loc;
char *libc_paths[5] = {
		"here will be SIM_LIBC_PATH env",
		"/lib64/libc.so.6",
		"/lib/libc.so.6",
		"/lib/x86_64-linux-gnu/libc.so.6",
		NULL};

extern int64_t *sim_timeval_shift;
extern double *sim_timeval_scale;

int64_t process_create_time_real = 0;
int64_t process_create_time_sim = 0;


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

int find_nth_space(char *search_buffer, int space_ordinality) {
	int jndex;
	int space_count;

	space_count = 0;

	for (jndex = 0; search_buffer[jndex]; jndex++) {
		if (search_buffer[jndex] == ' ') {
			space_count++;

			if (space_count >= space_ordinality) {
				return jndex;
			}
		}
	}

	fprintf(stderr, "looking for too many spaces\n");

	exit(1);

}

/* return process create time in microseconds */
int64_t get_process_create_time() {
	int field_begin;
	int stat_fd;

	const int stat_buf_size = 8192;
	char *stat_buf = xcalloc(stat_buf_size,1);

	long jiffies_per_second;

	int64_t boot_time_since_epoch;
	int64_t process_start_time_since_boot;

	int64_t process_start_time_since_epoch;

	ssize_t read_result;

	jiffies_per_second = sysconf(_SC_CLK_TCK);


	stat_fd = open("/proc/self/stat", O_RDONLY);

	if (stat_fd < 0) {
		fprintf(stderr, "open() fail\n");
		exit(1);
	}

	read_result = read(stat_fd, stat_buf, stat_buf_size);

	if (read_result < 0) {
		fprintf(stderr, "read() fail\n");
		exit(1);
	}

	if (read_result >= stat_buf_size) {
		fprintf(stderr, "stat_buf is too small\n");
		exit(1);
	}

	field_begin = find_nth_space(stat_buf, 21) + 1;

	stat_buf[find_nth_space(stat_buf, 22)] = 0;

	sscanf(stat_buf + field_begin, "%" PRId64, &process_start_time_since_boot);

	close(stat_fd);

	stat_fd = open("/proc/stat", O_RDONLY);

	if (stat_fd < 0) {
		fprintf(stderr, "open() fail\n");

		exit(1);
	}

	read_result = read(stat_fd, stat_buf, stat_buf_size);

	if (read_result < 0) {
		fprintf(stderr, "read() fail\n");

		exit(1);
	}

	if (read_result >= stat_buf_size) {
		fprintf(stderr, "stat_buf is too small\n");

		exit(1);
	}

	close(stat_fd);

	field_begin = strstr(stat_buf, "btime ") - stat_buf + 6;
	sscanf(stat_buf + field_begin, "%" PRId64, &boot_time_since_epoch);

	if(jiffies_per_second<=10000) {
		process_start_time_since_epoch = boot_time_since_epoch * 1000000
					+ (process_start_time_since_boot * 1000000) / jiffies_per_second;
	} else {
		double dtmp1=((double)process_start_time_since_boot/(double)jiffies_per_second)*1.0e6;
		process_start_time_since_epoch = boot_time_since_epoch * 1000000 + (int64_t)dtmp1;
	}

	xfree(stat_buf);
	return process_start_time_since_epoch;
}

void init_sim_time(uint32_t start_time, double scale, int set_time, int set_time_to_real)
{
	int64_t cur_sim_time;
	int64_t cur_real_time;

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

	cur_sim_time = get_sim_utime();
	cur_real_time = get_real_utime();

	process_create_time_real = get_process_create_time();
	process_create_time_sim = process_create_time_real + (cur_sim_time - cur_real_time);

	info("sim: process create utime: %" PRId64 " process create utime: %" PRId64,
			process_create_time_real, process_create_time_sim);
	info("sim: current real utime: %" PRId64 ", current sim utime: %" PRId64,
			cur_real_time, cur_sim_time);
}

void slurm_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
	int nanosecondswait=1000000;
	int64_t abstime_sim = abstime->tv_sec * 1000000 + (abstime->tv_nsec/1000);
	int64_t real_utime = get_real_utime();
	int64_t sim_utime = get_sim_utime();
	int64_t abstime_real = abstime_sim + (real_utime-sim_utime);
	int64_t next_real_time;
	struct timespec ts;
	int err;
	struct timespec abstime_real_ts;

	abstime_real_ts.tv_sec = abstime_real/1000000;
	abstime_real_ts.tv_nsec = (abstime_real%1000000)*1000;

	do {
		timespec_get(&ts, TIME_UTC);

		ts.tv_nsec = ts.tv_nsec + nanosecondswait;

		if(ts.tv_nsec >=  1000000000) {
			ts.tv_sec += ts.tv_nsec / 1000000000;
			ts.tv_nsec = ts.tv_nsec % 1000000000;
		}

		next_real_time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

		if(next_real_time < abstime_real) {
			next_real_time = abstime_real;
		}
		err = pthread_cond_timedwait(cond, mutex, &abstime_real_ts);
		if (err && (err != ETIMEDOUT)) {
			errno = err;
			error("%s:%d %s: pthread_cond_timedwait(): %m",
				  __FILE__, __LINE__, __func__);
			break;
		}
		if (err==0) {
			// i.e. got signal
			break;
		}

	} while (get_sim_utime() < abstime_sim);
}

void iso8601_from_utime(char **buf, uint64_t utime, bool msec)
{
	char p[64] = "";
	struct timeval tv;
	struct tm tm;



	tv.tv_sec = utime / 1000000;
	tv.tv_usec = utime % 1000000;

	if (!localtime_r(&tv.tv_sec, &tm))
		fprintf(stderr, "localtime_r() failed\n");

	if (strftime(p, sizeof(p), "%Y-%m-%dT%T", &tm) == 0)
		fprintf(stderr, "strftime() returned 0\n");

	if (msec)
		_xstrfmtcat(buf, "%s.%3.3d", p, (int)(tv.tv_usec / 1000));
	else
		_xstrfmtcat(buf, "%s", p);
}

