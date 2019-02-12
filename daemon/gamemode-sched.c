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

#define _GNU_SOURCE

#include "daemon_config.h"
#include "gamemode.h"
#include "logging.h"

#include <errno.h>
#include <sched.h>
#include <string.h>
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
 *
 * We don't need to store the current values because when the client exits,
 * everything will be good: Scheduling is only applied to the client and
 * its children.
 */
void game_mode_apply_renice(const GameModeContext *self, const pid_t client)
{
	GameModeConfig *config = game_mode_config_from_context(self);

	/*
	 * read configuration "renice" (1..20)
	 */
	long int renice = 0;
	config_get_renice_value(config, &renice);
	if ((renice < 1) || (renice > 20)) {
		LOG_ONCE(ERROR, "Configured renice value '%ld' is invalid, will not renice.\n", renice);
		return;
	} else {
		renice = -renice;
	}

	/*
	 * don't adjust priority if it was already adjusted
	 */
	if (getpriority(PRIO_PROCESS, (id_t)client) != 0) {
		LOG_ERROR("Refused to renice client [%d]: already reniced\n", client);
	} else if (setpriority(PRIO_PROCESS, (id_t)client, (int)renice)) {
		LOG_HINTED(ERROR,
		           "Failed to renice client [%d], ignoring error condition: %s\n",
		           "    -- Your user may not have permission to do this. Please read the docs\n"
		           "    -- to learn how to adjust the pam limits.\n",
		           client,
		           strerror(errno));
	}
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
	bool enable_softrealtime = (strcmp(softrealtime, "on") == 0) || (get_nprocs() > 3);

	/*
	 * Actually apply the scheduler policy if not explicitly turned off
	 */
	if (!(strcmp(softrealtime, "off") == 0) && (enable_softrealtime)) {
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
	} else {
		LOG_ERROR("Skipped setting client [%d] into SCHED_ISO mode: softrealtime setting is '%s'\n",
		          client,
		          softrealtime);
	}
}
