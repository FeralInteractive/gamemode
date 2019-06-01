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
#include "common-logging.h"
#include "gamemode-config.h"

#include <dirent.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>

/* SCHED_ISO may not be defined as it is a reserved value not yet
 * implemented in official kernel sources, see linux/sched.h.
 */
#ifndef SCHED_ISO
#define SCHED_ISO 4
#endif

/**
 * Apply scheduling policies
 *
 * This tries to change the scheduler of the client to soft realtime mode
 * available in some kernels as SCHED_ISO. It also tries to adjust the nice
 * level. If some of each fail, ignore this and log a warning.
 */

#define RENICE_INVALID -128 /* Special value to store invalid value */
int game_mode_get_renice(const pid_t client)
{
	/* Clear errno as -1 is a regitimate return */
	errno = 0;
	int priority = getpriority(PRIO_PROCESS, (id_t)client);
	if (priority == -1 && errno) {
		LOG_ERROR("getprority(PRIO_PROCESS, %d) failed : %s\n", client, strerror(errno));
		return RENICE_INVALID;
	}
	return -priority;
}

/* If expected is 0 then we try to apply our renice, otherwise, we try to remove it */
void game_mode_apply_renice(const GameModeContext *self, const pid_t client, int expected)
{
	if (expected == RENICE_INVALID)
		/* Silently bail if fed an invalid value */
		return;

	GameModeConfig *config = game_mode_config_from_context(self);

	/*
	 * read configuration "renice" (1..20)
	 */
	long int renice = config_get_renice_value(config);
	if (renice == 0) {
		return;
	}

	/* Invert the renice value */
	renice = -renice;

	/* When expected is non-zero, we should try and remove the renice only if it doesn't match the
	 * expected value */
	if (expected != 0) {
		expected = (int)renice;
		renice = 0;
	}

	/* Open the tasks dir for the client */
	char tasks[128];
	snprintf(tasks, sizeof(tasks), "/proc/%d/task", client);
	DIR *client_task_dir = opendir(tasks);
	if (client_task_dir == NULL) {
		LOG_ERROR("Could not inspect tasks for client [%d]! Skipping ioprio optimisation.\n",
		          client);
		return;
	}

	/* Iterate for all tasks of client process */
	struct dirent *tid_entry;
	while ((tid_entry = readdir(client_task_dir)) != NULL) {
		/* Skip . and .. */
		if (tid_entry->d_name[0] == '.')
			continue;

		/* task name is the name of the file */
		int tid = atoi(tid_entry->d_name);

		/* Clear errno as -1 is a regitimate return */
		errno = 0;
		int prio = getpriority(PRIO_PROCESS, (id_t)tid);

		if (prio == -1 && errno) {
			/* Process may well have ended */
			LOG_ERROR("getpriority failed for client [%d,%d] with error: %s\n",
			          client,
			          tid,
			          strerror(errno));
		} else if (prio != expected) {
			/*
			 * Don't adjust priority if it does not match the expected value
			 * ie. Another process has changed it, or it began non-standard
			 */
			LOG_ERROR("Refused to renice client [%d,%d]: prio was (%d) but we expected (%d)\n",
			          client,
			          tid,
			          prio,
			          expected);
		} else if (setpriority(PRIO_PROCESS, (id_t)tid, (int)renice)) {
			LOG_HINTED(ERROR,
			           "Failed to renice client [%d,%d], ignoring error condition: %s\n",
			           "    -- Your user may not have permission to do this. Please read the docs\n"
			           "    -- to learn how to adjust the pam limits.\n",
			           client,
			           tid,
			           strerror(errno));
		}
	}

	closedir(client_task_dir);
}

void game_mode_apply_scheduling(const GameModeContext *self, const pid_t client)
{
	GameModeConfig *config = game_mode_config_from_context(self);

	/*
	 * read configuration "softrealtime" (on, off, auto)
	 */
	char softrealtime[CONFIG_VALUE_MAX] = { 0 };
	config_get_soft_realtime(config, softrealtime);

	/*
	 * Enable unconditionally or auto-detect soft realtime usage,
	 * auto detection is based on observations where dual-core CPU suffered
	 * priority inversion problems with the graphics driver thus running
	 * slower as a result, so enable only with more than 3 cores.
	 */
	bool enable_softrealtime = (strcmp(softrealtime, "on") == 0) ||
	                           ((strcmp(softrealtime, "auto") == 0) && (get_nprocs() > 3));

	/*
	 * Actually apply the scheduler policy if not explicitly turned off
	 */
	if (enable_softrealtime) {
		const struct sched_param p = { .sched_priority = 0 };
		if (sched_setscheduler(client, SCHED_ISO | SCHED_RESET_ON_FORK, &p)) {
			const char *hint = "";
			HINT_ONCE_ON(
			    errno == EPERM,
			    hint,
			    "    -- The error indicates that you may be running a resource management\n"
			    "    -- daemon managing your game launcher and it leaks lower scheduling\n"
			    "    -- classes into the games. This is likely a bug in the management daemon\n"
			    "    -- and not a bug in GameMode, it should be reported upstream.\n"
			    "    -- If unsure, please also look here:\n"
			    "    -- https://github.com/FeralInteractive/gamemode/issues/68\n");
			HINT_ONCE_ON(
			    errno == EINVAL,
			    hint,
			    "    -- The error indicates that your kernel may not support this. If you\n"
			    "    -- don't know what SCHED_ISO means, you can safely ignore this. If you\n"
			    "    -- expected it to work, ensure you're running a kernel with MuQSS or\n"
			    "    -- PDS scheduler.\n"
			    "    -- For further technical reading on the topic start here:\n"
			    "    -- https://lwn.net/Articles/720227/\n");
			LOG_ERROR(
			    "Failed setting client [%d] into SCHED_ISO mode, ignoring error condition: %s\n"
			    "%s",
			    client,
			    strerror(errno),
			    hint);
		}
	}
}
