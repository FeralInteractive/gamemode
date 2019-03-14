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

#include "external-helper.h"
#include "logging.h"

#include <linux/limits.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const int default_timeout = 5;

/**
 * Call an external process
 */
int run_external_process(const char *const *exec_args, char buffer[EXTERNAL_BUFFER_MAX], int tsec)
{
	pid_t p;
	int status = 0;
	int pipes[2];
	char internal[EXTERNAL_BUFFER_MAX] = { 0 };

	if (pipe(pipes) == -1) {
		LOG_ERROR("Could not create pipe: %s!\n", strerror(errno));
		return -1;
	}

	/* Set the default timeout */
	if (tsec == -1) {
		tsec = default_timeout;
	}

	/* set up our signaling for the child and the timout */
	sigset_t mask;
	sigset_t omask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &mask, &omask) < 0) {
		LOG_ERROR("sigprocmask failed: %s\n", strerror(errno));
		return -1;
	}

	if ((p = fork()) < 0) {
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
		_exit(EXIT_SUCCESS);
	}

	/* Set up the timout */
	struct timespec timeout;
	timeout.tv_sec = tsec;
	timeout.tv_nsec = 0;

	/* Wait for the child to finish up with a timout */
	while (true) {
		if (sigtimedwait(&mask, NULL, &timeout) < 0) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN) {
				LOG_ERROR("Child process timed out for %s, killing and returning\n", exec_args[0]);
				kill(p, SIGKILL);
			} else {
				LOG_ERROR("sigtimedwait failed: %s\n", strerror(errno));
				return -1;
			}
		}
		break;
	}

	close(pipes[1]);
	ssize_t output_size = read(pipes[0], internal, EXTERNAL_BUFFER_MAX - 1);
	if (output_size < 0) {
		LOG_ERROR("Failed to read from process %s: %s\n", exec_args[0], strerror(errno));
		return -1;
	}

	internal[output_size] = 0;

	if (waitpid(p, &status, 0) < 0) {
		LOG_ERROR("Failed to waitpid(%d): %s\n", (int)p, strerror(errno));
		return -1;
	}

	/* i.e. sigsev */
	if (!WIFEXITED(status)) {
		LOG_ERROR("Child process '%s' exited abnormally\n", exec_args[0]);
	} else if (WEXITSTATUS(status) != 0) {
		LOG_ERROR("External process failed with exit code %u\n", WEXITSTATUS(status));
		LOG_ERROR("Output was: %s\n", buffer ? buffer : internal);
		return -1;
	}

	if (buffer)
		memcpy(buffer, internal, EXTERNAL_BUFFER_MAX);

	return 0;
}
