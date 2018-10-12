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

#include "gamemode.h"

#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Open the process environment for enumerating or lookup. Requires an open
 * directory FD from /proc/PID.
 */
FILE *game_mode_open_proc_env(const procfd_t proc_fd)
{
	/* Try to open the environ file of the process */
	int fd = openat(proc_fd, "environ", O_RDONLY | O_CLOEXEC);
	if (fd != -1) {
		FILE *stream = fdopen(fd, "r");
		if (stream)
			return stream;
		else
			close(fd);
	}
	/* We failed */
	return NULL;
}

/**
 * Close the process enviroment opened by game_mode_open_proc_env().
 */
int game_mode_close_proc_env(FILE *stream)
{
	return fclose(stream);
}

/**
 * Lookup the process environment for a specific variable or return NULL.
 * Requires an open directory FD from /proc/PID.
 */
char *game_mode_lookup_proc_env(const procfd_t proc_fd, const char *var)
{
	char *environ = NULL;

	FILE *stream = game_mode_open_proc_env(proc_fd);
	if (stream) {
		/* Read every \0 terminated line from the environment */
		char *line = NULL;
		size_t len = 0;
		size_t pos = strlen(var) + 1;
		while (!environ && (getdelim(&line, &len, 0, stream) != -1)) {
			/* Find a match including the "=" suffix */
			if ((len > pos) && (strncmp(line, var, strlen(var)) == 0) && (line[pos - 1] == '='))
				environ = strndup(line + pos, len - pos);
		}
		free(line);
		game_mode_close_proc_env(stream);
	}

	/* If found variable is empty, skip it */
	if (environ && !strlen(environ)) {
		free(environ);
		environ = NULL;
	}

	return environ;
}

/**
 * Lookup the home directory of the user in a safe way.
 */
char *game_mode_lookup_user_home(void)
{
	/* Try loading env HOME first */
	const char *home = secure_getenv("HOME");
	if (!home) {
		/* If HOME is not defined (or out of context), fall back to passwd */
		struct passwd *pw = getpwuid(getuid());
		if (!pw)
			return NULL;
		home = pw->pw_dir;
	}

	/* Try to allocate into our heap */
	return home ? strdup(home) : NULL;
}
