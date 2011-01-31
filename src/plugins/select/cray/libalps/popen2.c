/*
 *     Implementation of a coprocess forked as child.
 *
 *     +--------------------------------------------+
 *     |                 PARENT                     |
 *     |                                            |
 *     |    in                            out       |
 *     | child_in[1]                  child_out[0]  |
 *     +--------------------------------------------+
 *           |                             ^
 *           |                             |
 *           V                             |
 *     +--------------------------------------------+
 *     | child_in[0]                  child_out[1]  |
 *     |     |                             |        |
 *     | STDIN_FILENO                STDOUT_FILENO  |
 *     |                                            |
 *     |                  CHILD                     |
 *     +--------------------------------------------+
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include <unistd.h>
#include <stdbool.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <err.h>

static int dup_devnull(int stream_fd)
{
	int fd = open("/dev/null", O_RDWR);

	if (fd == -1 || dup2(fd, stream_fd) < 0)
		return -1;
	return close(fd);
}

/* move @fd out of the way of stdin/stdout/stderr */
static int fd_no_clobber_stdio(int *fd)
{
	if (*fd <= STDERR_FILENO) {
		int moved_fd = fcntl(*fd, F_DUPFD, STDERR_FILENO + 1);

		if (moved_fd < 0 || close(*fd) < 0)
			return -1;
		*fd = moved_fd;
	}
	return 0;
}

/**
 * popen2  -  Open a bidirectional pipe to a process
 * @command:	command to execute as a child co-process
 * @to_child:	return file descriptor for child stdin
 * @from_child:	return file descriptor for child stdout
 *
 * Return -1 on error, pid of child process on success.
 */
pid_t popen2(char *command, int *to_child, int *from_child, bool no_stderr)
{
	int child_in[2], child_out[2];
	pid_t pid;

	if (access(command, X_OK) < 0)
		return -1;

	if (pipe(child_in) < 0 || pipe(child_out) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0) {
		/*
		 * child
		 */
		close(child_in[1]);
		close(child_out[0]);

		/*
		 * Rationale: by making sure that fd > 2, dup(fd) <= 2,
		 * a new copy is created, allowing to close the pipe end,
		 * and with FD_CLOEXEC to off (on linux).
		 */
		if (fd_no_clobber_stdio(&child_in[0]) < 0		||
		    dup2(child_in[0], STDIN_FILENO) != STDIN_FILENO	||
		    close(child_in[0]) < 0)
			_exit(127);

		if (fd_no_clobber_stdio(&child_out[1]) < 0		||
		    dup2(child_out[1], STDOUT_FILENO) != STDOUT_FILENO	||
		    close(child_out[1]) < 0)
			_exit(127);

		if (no_stderr && dup_devnull(STDERR_FILENO) < 0)
			_exit(127);

		if (execl(command, basename(command), NULL) < 0)
			_exit(1);
	}

	/*
	 * parent
	 */
	close(child_in[0]);
	close(child_out[1]);

	*to_child   = child_in[1];
	*from_child = child_out[0];

	return pid;
}

/*
 * From git sources: wait for child termination
 * Return 0 if child exited with 0, a positive value < 256 on error.
 */
unsigned char wait_for_child(pid_t pid)
{
	int status, failed_errno = 0;
	unsigned char code = -1;
	pid_t waiting;

	do {
		waiting = waitpid(pid, &status, 0);
	} while (waiting < 0 && errno == EINTR);

	if (waiting < 0) {
		failed_errno = errno;
	} else if (WIFSIGNALED(status)) {
		code = WTERMSIG(status);
		/*
		 * This return value is chosen so that code & 0xff
		 * mimics the exit code that a POSIX shell would report for
		 * a program that died from this signal.
		 */
		code -= 128;
	} else if (WIFEXITED(status)) {
		code = WEXITSTATUS(status);
		/*
		 * Convert special exit code when execvp failed.
		 */
		if (code == 127) {
			code = -1;
			failed_errno = ENOENT;
		}
	}
	errno = failed_errno;
	return code;
}
