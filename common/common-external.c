/*

Copyright (c) 2017-2019, Feral Interactive
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
 * Neither the name of Feral Interactive nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

 */

#define _GNU_SOURCE

#include "common-external.h"
#include "common-logging.h"

#include <sys/wait.h>
#include <unistd.h>

static const int DEFAULT_TIMEOUT = 5;

static int read_child_stdout(int pipe_fd, char buffer[EXTERNAL_BUFFER_MAX], int tsec)
{
	fd_set fds;
	struct timeval timeout;
	int num_readable = 0;
	ssize_t buffer_bytes_read = 0;
	ssize_t just_read = 0;
	bool buffer_full = false;
	char discard_buffer[EXTERNAL_BUFFER_MAX];

	/* Set up the timout */
	timeout.tv_sec = tsec;
	timeout.tv_usec = 0;

	FD_ZERO(&fds);

	/* Wait for the child to finish up with a timout */
	while (true) {
		FD_SET(pipe_fd, &fds);
		num_readable = select(pipe_fd + 1, &fds, NULL, NULL, &timeout);

		if (num_readable < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				LOG_ERROR("sigtimedwait failed: %s\n", strerror(errno));
				return -1;
			}
		} else if (num_readable == 0) {
			return -2;
		}

		if (!buffer_full) {
			just_read = read(pipe_fd,
			                 buffer + buffer_bytes_read,
			                 EXTERNAL_BUFFER_MAX - (size_t)buffer_bytes_read - 1);
		} else {
			just_read = read(pipe_fd, discard_buffer, EXTERNAL_BUFFER_MAX - 1);
		}

		if (just_read < 0) {
			return -1;
		} else if (just_read == 0) {
			// EOF encountered
			break;
		}

		if (!buffer_full) {
			buffer_bytes_read += just_read;

			if (buffer_bytes_read == EXTERNAL_BUFFER_MAX - 1) {
				// our buffer is exhausted, discard the rest
				// of the output
				buffer_full = true;
			}
		}
	}

	buffer[buffer_bytes_read] = 0;

	return 0;
}

/**
 * Call an external process
 */
int run_external_process(const char *const *exec_args, char buffer[EXTERNAL_BUFFER_MAX], int tsec)
{
	pid_t p;
	int status = 0;
	int pipes[2];
	int ret = 0;
	char internal[EXTERNAL_BUFFER_MAX] = { 0 };

	if (pipe(pipes) == -1) {
		LOG_ERROR("Could not create pipe: %s!\n", strerror(errno));
		return -1;
	}

	/* Set the default timeout */
	if (tsec == -1) {
		tsec = DEFAULT_TIMEOUT;
	}

	if ((p = fork()) < 0) {
		close(pipes[0]);
		close(pipes[1]);
		LOG_ERROR("Failed to fork(): %s\n", strerror(errno));
		return false;
	} else if (p == 0) {
		/* Send STDOUT to the pipe */
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		/* Execute the command */
		/* Note about cast:
		 *   The statement about argv[] and envp[] being constants is
		 *   included to make explicit to future writers of language
		 *   bindings that these objects are completely constant.
		 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html
		 */
		if (execv(exec_args[0], (char *const *)exec_args) != 0) {
			LOG_ERROR("Failed to execute external process: %s %s\n", exec_args[0], strerror(errno));
			exit(EXIT_FAILURE);
		}
		// should never be reached
		abort();
	}

	// close the write end of the pipe so we get signaled EOF once the
	// child exits
	close(pipes[1]);
	ret = read_child_stdout(pipes[0], internal, tsec);
	close(pipes[0]);

	if (ret != 0) {
		if (ret == -2) {
			LOG_ERROR("Child process timed out for %s, killing and returning\n", exec_args[0]);
			kill(p, SIGKILL);
		} else {
			LOG_ERROR("Failed to read from process %s: %s\n", exec_args[0], strerror(errno));
		}
		if (buffer) {
			// make sure the buffer is a terminated empty string on error
			buffer[0] = 0;
		}
	} else if (buffer) {
		memcpy(buffer, internal, EXTERNAL_BUFFER_MAX);
	}

	if (waitpid(p, &status, 0) < 0) {
		LOG_ERROR("Failed to waitpid(%d): %s\n", (int)p, strerror(errno));
		return -1;
	}

	/* i.e. sigsev */
	if (!WIFEXITED(status)) {
		LOG_ERROR("Child process '%s' exited abnormally\n", exec_args[0]);
	} else if (WEXITSTATUS(status) != 0) {
		LOG_ERROR("External process failed with exit code %d\n", WEXITSTATUS(status));
		LOG_ERROR("Output was: %s\n", internal);
		return -1;
	}

	return 0;
}
