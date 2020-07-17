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
#include "common-helpers.h"
#include "common-logging.h"

#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>

/**
 * Detect if the process is a wine preloader process
 */
static bool game_mode_detect_wine_preloader(const char *exe)
{
	return (strtail(exe, "/wine-preloader") || strtail(exe, "/wine64-preloader"));
}

/**
 * Detect if the process is a wine loader process
 */
static bool game_mode_detect_wine_loader(const char *exe)
{
	return (strtail(exe, "/wine") || strtail(exe, "/wine64"));
}

/**
 * Opens the process environment for a specific PID and returns
 * a file descriptor to the directory /proc/PID. Doing it that way prevents
 * the directory going MIA when a process exits while we are looking at it
 * and allows us to handle fewer error cases.
 */
static procfd_t game_mode_open_proc(const pid_t pid)
{
	char buffer[PATH_MAX];
	const char *proc_path = buffered_snprintf(buffer, "/proc/%d", pid);

	return proc_path ? open(proc_path, O_RDONLY | O_CLOEXEC) : INVALID_PROCFD;
}

/**
 * Closes the process environment.
 */
static int game_mode_close_proc(const procfd_t procfd)
{
	return close(procfd);
}

/**
 * Lookup the process environment for a specific variable or return NULL.
 * Requires an open directory FD from /proc/PID.
 */
static char *game_mode_lookup_proc_env(const procfd_t proc_fd, const char *var)
{
	char *environ = NULL;

	int fd = openat(proc_fd, "environ", O_RDONLY | O_CLOEXEC);
	if (fd != -1) {
		FILE *stream = fdopen(fd, "r");
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
			fclose(stream);
		} else
			close(fd);
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
static char *game_mode_lookup_user_home(void)
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

/**
 * Attempt to resolve the exe for wine-preloader.
 * This function is used if game_mode_context_find_exe() identified the
 * process as wine-preloader. Returns NULL when resolve fails.
 */
char *game_mode_resolve_wine_preloader(const char *exe, const pid_t pid)
{
	/* Detect if the process is a wine loader process */
	if (game_mode_detect_wine_preloader(exe) || game_mode_detect_wine_loader(exe)) {
		LOG_MSG("Detected wine for client %d [%s].\n", pid, exe);
	} else {
		return NULL;
	}

	char buffer[PATH_MAX];
	char *wine_exe = NULL, *wineprefix = NULL;

	/* Open the directory, we are potentially reading multiple files from it */
	procfd_t proc_fd = game_mode_open_proc(pid);

	if (proc_fd == INVALID_PROCFD)
		goto fail_proc;

	/* Open the command line */
	int fd = openat(proc_fd, "cmdline", O_RDONLY | O_CLOEXEC);
	if (fd != -1) {
		FILE *stream = fdopen(fd, "r");
		if (stream) {
			char *argv = NULL;
			size_t args = 0;
			int argc = 0;
			while (!wine_exe && (argc++ < 2) && (getdelim(&argv, &args, 0, stream) != -1)) {
				/* If we see the wine loader here, we have to use the next argument */
				if (strtail(argv, "/wine") || strtail(argv, "/wine64"))
					continue;
				free(wine_exe); // just in case
				/* Check presence of the drive letter, we assume that below */
				wine_exe = args > 2 && argv[1] == ':' ? strndup(argv, args) : NULL;
			}
			free(argv);
			fclose(stream);
		} else
			close(fd);
	}

	/* Did we get wine exe from cmdline? */
	if (wine_exe)
		LOG_MSG("Detected wine exe for client %d [%s].\n", pid, wine_exe);
	else
		goto fail_cmdline;

	/* Open the process environment and find the WINEPREFIX */
	errno = 0;
	if (!(wineprefix = game_mode_lookup_proc_env(proc_fd, "WINEPREFIX"))) {
		/* Lookup user home instead only if there was no error */
		char *home = NULL;
		if (errno == 0)
			home = game_mode_lookup_user_home();

		/* Append "/.wine" if we found the user home */
		if (home)
			wineprefix = safe_snprintf(buffer, "%s/.wine", home);

		/* Cleanup and check result */
		free(home);
		if (!wineprefix)
			goto fail_env;
	}

	/* Wine prefix was detected, log this for diagnostics */
	LOG_MSG("Detected wine prefix for client %d: '%s'\n", pid, wineprefix);

	/* Convert Windows to Unix path separators */
	char *ix = wine_exe;
	while (ix != NULL)
		(ix = strchr(ix, '\\')) && (*ix++ = '/');

	/* Convert the drive letter to lcase because wine handles it this way in the prefix */
	wine_exe[0] = (char)tolower(wine_exe[0]);

	/* Convert relative wine exe path to full unix path */
	char *wine_path = buffered_snprintf(buffer, "%s/dosdevices/%s", wineprefix, wine_exe);
	free(wine_exe);
	wine_exe = wine_path ? realpath(wine_path, NULL) : NULL;

	/* Fine? Successo? Fortuna! */
	if (wine_exe)
		LOG_MSG("Successfully mapped wine client %d [%s].\n", pid, wine_exe);
	else
		goto fail;

error_cleanup:
	if (proc_fd != INVALID_PROCFD)
		game_mode_close_proc(proc_fd);
	free(wineprefix);
	return wine_exe;

fail:
	LOG_ERROR("Unable to find wine executable for client %d: %s\n", pid, strerror(errno));
	goto error_cleanup;

fail_cmdline:
	LOG_ERROR("Wine loader has no accepted cmdline for client %d yet, deferring.\n", pid);
	goto error_cleanup;

fail_env:
	LOG_ERROR("Failed to access process environment for client %d: %s\n", pid, strerror(errno));
	goto error_cleanup;

fail_proc:
	LOG_ERROR("Failed to access process data for client %d: %s\n", pid, strerror(errno));
	goto error_cleanup;
}
