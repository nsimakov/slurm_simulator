# Makefile for jobcomp/filesacctout plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_filesacctout.la

# Text file job completion logging plugin.
jobcomp_filesacctout_la_SOURCES = jobcomp_filesacctout.c \
			filesacctout_jobcomp_process.c filesacctout_jobcomp_process.h

jobcomp_filesacctout_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
