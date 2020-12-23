#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

# noinst_LTLIBRARIES = libslurmctld.la
# libslurmctld_la_LDFLAGS  = $(LIB_LDFLAGS) -module --export-dynamic
# libslurmctld_la_SOURCES =
slurmctld_SOURCES =     \
	acct_policy.c	\
	acct_policy.h	\
	agent.h		\
	backup.c	\
	burst_buffer.c	\
	burst_buffer.h	\
	fed_mgr.c 	\
	fed_mgr.h 	\
	front_end.c	\
	front_end.h	\
	gang.c		\
	gang.h		\
	groups.c	\
	groups.h	\
	heartbeat.c	\
	heartbeat.h	\
	job_mgr.c 	\
	job_scheduler.c	\
	job_scheduler.h	\
	job_submit.c	\
	job_submit.h	\
	licenses.c	\
	licenses.h	\
	locks.c   	\
	locks.h  	\
	node_mgr.c 	\
	node_scheduler.c \
	node_scheduler.h \
	partition_mgr.c \
	ping_nodes.c	\
	ping_nodes.h	\
	port_mgr.c	\
	port_mgr.h	\
	power_save.c	\
	power_save.h	\
	powercapping.c	\
	powercapping.h	\
	preempt.c	\
	preempt.h	\
	prep_slurmctld.c \
	proc_req.c	\
	proc_req.h	\
	read_config.c	\
	read_config.h	\
	reservation.c	\
	reservation.h	\
	sched_plugin.c	\
	sched_plugin.h	\
	slurmctld.h	\
	slurmctld_plugstack.c \
	slurmctld_plugstack.h \
	srun_comm.c	\
	srun_comm.h	\
	state_save.c	\
	state_save.h	\
	statistics.c	\
	step_mgr.c	\
	trigger_mgr.c	\
	trigger_mgr.h

if ENABLE_SIMULATOR
slurmctld_SOURCES +=	\
	../common/sim/sim_slurmctld.c	\
	../common/sim/sim_slurmctld_agent.c	\
	../common/sim/sim_slurmd.c	\
	../common/sim/sim_slurmd_req.c	\
	../common/sim/sim_slurmd_get_mach_stat.c	\
	../common/sim/sim_events.c ../common/sim/sim_jobs.c	\
	../common/sim/sim_sbatch.c ../sbatch/opt.c opt.h ../sbatch/xlate.c
else
slurmctld_SOURCES += controller.c agent.c
endif

sbin_PROGRAMS = slurmctld

depend_libs = $(top_builddir)/src/common/libdaemonize.la
depend_ldadd =
depend_ldflags = 

if ENABLE_SIMULATOR
depend_libs += \
	$(top_builddir)/src/bcast/libfile_bcast.la \
	$(top_builddir)/src/slurmd/common/libslurmd_common.o $(top_builddir)/src/slurmd/common/libslurmd_reverse_tree_math.la
depend_ldadd += $(NUMA_LIBS)
# These 2 depend_* should look the same in ../slurmstepd/Makefile.am
depend_ldadd += $(HWLOC_LIBS) $(PAM_LIBS) $(UTIL_LIBS)
depend_ldflags += $(HWLOC_LDFLAGS)

# This is needed to actually link to the libs that it doesn't use.
depend_ldflags += -Wl,--no-as-needed
# same function waitpid_timeout defined twice rename one
slurmctld_CFLAGS = -Dwaitpid_timeout=slurmctld_waitpid_timeout

endif

slurmctld_LDADD = $(depend_libs) $(LIB_SLURM) $(DL_LIBS) $(depend_ldadd)
slurmctld_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(depend_ldflags)

if ENABLE_SIMULATOR
slurmctld_LDFLAGS += -Wl,-wrap,pthread_create -Wl,-wrap,xsignal_block
# slurmctld_LDFLAGS += -Wl,-wrap,agent_init -Wl,-wrap,fed_mgr_init
# slurmctld_LDFLAGS += 
endif

slurmctld_DEPENDENCIES = $(LIB_SLURM_BUILD) $(depend_libs)

force:
$(slurmctld_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
