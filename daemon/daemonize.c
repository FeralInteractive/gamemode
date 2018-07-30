/*

Copyright (c) 2017-2018, Feral Interactive
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
#include "daemonize.h"
#include "logging.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * Helper to perform standard UNIX daemonization
 */
void daemonize(const char *name)
{
	/* Initial fork */
	pid_t pid = fork();
	if (pid < 0) {
		FATAL_ERRORNO("Failed to fork");
	}

	if (pid != 0) {
		LOG_MSG("Daemon launched as %s...\n", name);
		exit(EXIT_SUCCESS);
	}

	/* Fork a second time */
	pid = fork();
	if (pid < 0) {
		FATAL_ERRORNO("Failed to fork");
	} else if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Now continue execution */
	umask(0022);
	if (setsid() < 0) {
		FATAL_ERRORNO("Failed to create process group\n");
	}
	if (chdir("/") < 0) {
		FATAL_ERRORNO("Failed to change to root directory\n");
	}
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
}
