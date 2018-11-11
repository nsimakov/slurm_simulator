Slurm Simulator
--------------------------------------------------------
Center for Computational Research, University at Buffalo, SUNY

Slurm is an open source job scheduling system that is widely used in many small and large-scale HPC resources,
including almost all current XSEDE resources. Like all resource management programs, Slurm is highly tuneable, 
with many parametric settings that can significantly influence job throughput, overall system utilization and 
job wait times. Unfortunately, in many cases it is difficult to judge how modification of these parameters will 
affect the overall performance of the HPC resource.  For example, a given policy choice which changes a single 
Slurm parameter may have unintended and perhaps undesirable consequences for the overall performance of the 
HPC system.  Also, it may take days or even weeks to see what, if any, impact certain changes have on the 
scheduler performance and operation. For these reasons, attempting to tune system performance or implement new 
policy choices through changes in the Slurm parameters on a production HPC system is not practical.  In a real sense, 
HPC center personnel are often times operating in the dark with respect to tuning the Slurm parameter space to 
optimize job throughput or resource efficiency.  The ability to simulate a Slurm operating environment can therefore 
provide a means to improve an existing production system or predict the performance of a newly planned HPC system, 
without impacting the production instance of Slurm 

We have developed a standalone Slurm Simulator, which runs on a workstation or a single HPC node, that allows 
time accelerated simulation of workloads on HPC resources. Based on a modification of the actual Slurm code, 
the simulator can be used to study the effects of different Slurm parameters on HPC resource performance and 
to optimize these parameters to fit a particular need or policy, for example, maximizing throughput for a 
particular range of job sizes.  In the current implementation, the Slurm simulator can model historic or 
synthetic workloads of a single cluster.  For small clusters, the simulator can simulate as many as 17 days 
per hour depending on the job composition, and the Slurm configuration.


Toolkit and documentation for slurm simulator repositary:

> https://github.com/nsimakov/slurm_sim_tools

Slurm simulator repositary:

> https://github.com/nsimakov/slurm_simulator

Publications:

1. N.A. Simakov, R.L. DeLeon, M.D. Innus, M.D. Jones, J.P. White, S.M. Gallo, A.K. Patra, and T.R. Furlani. (2018) A Slurm Simulator: Implementation and Parametric Analysis. In: Jarvis S., Wright S., Hammond S. (eds) High Performance Computing Systems. Performance Modeling, Benchmarking, and Simulation. PMBS 2017. Lecture Notes in Computer Science, vol 10724. Springer, Cham. https://link.springer.com/chapter/10.1007/978-3-319-72971-8_10

2.	N.A. Simakov, R.L. DeLeon, M.D. Innus, M.D. Jones, J.P. White, S.M. Gallo, A.K. Patra, and T.R. Furlani. 2018. Slurm Simulator: Improving Slurm Scheduler Performance on Large HPC systems by Utilization of Multiple Controllers and Node Sharing. In Proceedings of the Practice and Experience on Advanced Research Computing (PEARC '18). ACM, New York, NY, USA, Article 25, 8 pages. DOI: https://doi.org/10.1145/3219104.3219111


Slurm Workload Manager
--------------------------------------------------------

This is the Slurm Workload Manager. Slurm
is an open-source cluster resource management and job scheduling system
that strives to be simple, scalable, portable, fault-tolerant, and
interconnect agnostic. Slurm currently has been tested only under Linux.

As a cluster resource manager, Slurm provides three key functions. First,
it allocates exclusive and/or non-exclusive access to resources
(compute nodes) to users for some duration of time so they can perform
work. Second, it provides a framework for starting, executing, and
monitoring work (normally a parallel job) on the set of allocated
nodes. Finally, it arbitrates conflicting requests for resources by
managing a queue of pending work.

NOTES FOR GITHUB DEVELOPERS
---------------------------

The official issue tracker for Slurm is at
  https://bugs.schedmd.com/

We welcome code contributions and patches, but **we do not accept Pull Requests
through Github at this time.** Please submit patches as attachments to new
bugs under "Contributions" category.

SOURCE DISTRIBUTION HIERARCHY
-----------------------------

The top-level distribution directory contains this README as well as
other high-level documentation files, and the scripts used to configure
and build Slurm (see INSTALL). Subdirectories contain the source-code
for Slurm as well as a DejaGNU test suite and further documentation. A
quick description of the subdirectories of the Slurm distribution follows:

  src/        [ Slurm source ]
     Slurm source code is further organized into self explanatory
     subdirectories such as src/api, src/slurmctld, etc.

  doc/        [ Slurm documentation ]
     The documentation directory contains some latex, html, and ascii
     text papers, READMEs, and guides. Manual pages for the Slurm
     commands and configuration files are also under the doc/ directory.

  etc/        [ Slurm configuration ]
     The etc/ directory contains a sample config file, as well as
     some scripts useful for running Slurm.

  slurm/      [ Slurm include files ]
     This directory contains installed include files, such as slurm.h
     and slurm_errno.h, needed for compiling against the Slurm API.

  testsuite/  [ Slurm test suite ]
     The testsuite directory contains the framework for a set of
     DejaGNU and "make check" type tests for Slurm components.
     There is also an extensive collection of Expect scripts.

  auxdir/     [ autotools directory ]
     Directory for autotools scripts and files used to configure and
     build Slurm

  contribs/   [ helpful tools outside of Slurm proper ]
     Directory for anything that is outside of slurm proper such as a
     different api or such.  To have this build you need to do a
     make contrib/install-contrib.

COMPILING AND INSTALLING THE DISTRIBUTION
-----------------------------------------

Please see the instructions at
  https://slurm.schedmd.com/quickstart_admin.html
Extensive documentation is available from our home page at
  https://slurm.schedmd.com/slurm.html

PROBLEMS
--------

If you experience problems compiling, installing, or running Slurm, see
   https://slurm.schedmd.com/help.html

LEGAL
-----

Slurm is provided "as is" and with no warranty. This software is
distributed under the GNU General Public License, please see the files
COPYING, DISCLAIMER, and LICENSE.OpenSSL for details.
