/*

Copyright (c) 2017, Feral Interactive
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

#include "governors.h"
#include "config.h"
#include "governors-query.h"
#include "logging.h"

#include <linux/limits.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *initial = NULL;

/**
 * Cache the governor state as seen at startup
 */
void update_initial_gov_state()
{
	initial = get_gov_state();
}

/**
 * Update the governors to the given argument, via pkexec
 */
bool set_governors(const char *value)
{
	pid_t p;
	int status = 0;
	int ret = 0;
	int r = -1;

	const char *govern = value ? value : initial;
	char *exec_args[] = {
		"/usr/bin/pkexec", LIBEXECDIR "/cpugovctl", "set", (char *)govern, NULL,
	};

	LOG_MSG("Requesting update of governor policy to %s\n", govern);

	if ((p = fork()) < 0) {
		LOG_ERROR("Failed to fork(): %s\n", strerror(errno));
		return false;
	} else if (p == 0) {
		/* Execute the command */
		if ((r = execv(exec_args[0], exec_args)) != 0) {
			LOG_ERROR("Failed to execute cpugovctl helper: %s %s\n", exec_args[1], strerror(errno));
			exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	} else {
		if (waitpid(p, &status, 0) < 0) {
			LOG_ERROR("Failed to waitpid(%d): %s\n", (int)p, strerror(errno));
			return false;
		}
		/* i.e. sigsev */
		if (!WIFEXITED(status)) {
			LOG_ERROR("Child process '%s' exited abnormally\n", exec_args[0]);
		}
	}

	if ((ret = WEXITSTATUS(status)) != 0) {
		LOG_ERROR("Failed to update cpu governor policy\n");
		return false;
	}

	return true;
}

/**
 * Return the cached governor seen at startup
 */
const char *get_initial_governor()
{
	return initial;
}
