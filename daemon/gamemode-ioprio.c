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
#include "gamemode-config.h"

#include <dirent.h>
#include <sys/syscall.h>

/**
 * Define the syscall interface in Linux because it is missing from glibc
 */

#ifndef IOPRIO_BITS
#define IOPRIO_BITS (16)
#endif

#ifndef IOPRIO_CLASS_SHIFT
#define IOPRIO_CLASS_SHIFT (13)
#endif

#ifndef IOPRIO_PRIO_MASK
#define IOPRIO_PRIO_MASK ((1UL << IOPRIO_CLASS_SHIFT) - 1)
#endif

#ifndef IOPRIO_PRIO_CLASS
#define IOPRIO_PRIO_CLASS(mask) ((mask) >> IOPRIO_CLASS_SHIFT)
#endif

#ifndef IOPRIO_PRIO_DATA
#define IOPRIO_PRIO_DATA(mask) ((mask)&IOPRIO_PRIO_MASK)
#endif

#ifndef IOPRIO_PRIO_VALUE
#define IOPRIO_PRIO_VALUE(class, data) (((class) << IOPRIO_CLASS_SHIFT) | data)
#endif

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

static inline int ioprio_set(int which, int who, int ioprio)
{
	return (int)syscall(SYS_ioprio_set, which, who, ioprio);
}

static inline int ioprio_get(int which, int who)
{
	return (int)syscall(SYS_ioprio_get, which, who);
}

/**
 * Get the i/o priorities
 */
int game_mode_get_ioprio(const pid_t client)
{
	int ret = ioprio_get(IOPRIO_WHO_PROCESS, client);
	if (ret == -1) {
		LOG_ERROR("Failed to get ioprio value for [%d] with error %s\n", client, strerror(errno));
		ret = IOPRIO_DONT_SET;
	}
	/* We support only IOPRIO_CLASS_BE as IOPRIO_CLASS_RT required CAP_SYS_ADMIN */
	return IOPRIO_PRIO_DATA(ret);
}

/**
 * Apply io priorities
 *
 * This tries to change the io priority of the client to a value specified
 * and can possibly reduce lags or latency when a game has to load assets
 * on demand.
 */
void game_mode_apply_ioprio(const GameModeContext *self, const pid_t client, int expected)
{
	if (expected == IOPRIO_DONT_SET)
		/* Silently bail if fed a don't set (invalid) */
		return;

	GameModeConfig *config = game_mode_config_from_context(self);

	/* read configuration "ioprio" (0..7) */
	int ioprio = (int)config_get_ioprio_value(config);

	/* Special value to simply not set the value */
	if (ioprio == IOPRIO_DONT_SET)
		return;

	LOG_MSG("Setting ioprio value...\n");

	/* If fed the default, we'll try and reset the value back */
	if (expected != IOPRIO_DEFAULT) {
		expected = (int)ioprio;
		ioprio = IOPRIO_DEFAULT;
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

		int current = game_mode_get_ioprio(tid);
		if (current == IOPRIO_DONT_SET) {
			/* Couldn't get the ioprio value
			 * This could simply mean that the thread exited before fetching the ioprio
			 * So we should continue
			 */
		} else if (current != expected) {
			/* Don't try and adjust the ioprio value if the value we got doesn't match default */
			LOG_ERROR("Skipping ioprio on client [%d,%d]: ioprio was (%d) but we expected (%d)\n",
			          client,
			          tid,
			          current,
			          expected);
		} else {
			/*
			 * For now we only support IOPRIO_CLASS_BE
			 * IOPRIO_CLASS_RT requires CAP_SYS_ADMIN but should be possible with a polkit process
			 */
			int p = ioprio;
			ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, ioprio);
			if (ioprio_set(IOPRIO_WHO_PROCESS, tid, ioprio) != 0) {
				/* This could simply mean the thread is gone now, as above */
				LOG_ERROR(
				    "Setting client [%d,%d] IO priority to (%d) failed with error %d, ignoring.\n",
				    client,
				    tid,
				    p,
				    errno);
			}
		}
	}

	closedir(client_task_dir);
}
