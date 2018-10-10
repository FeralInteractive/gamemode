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
#include "helpers.h"
#include "logging.h"

#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

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

/**
 * Apply io priorities
 *
 * This tries to change the io priority of the client to a value specified
 * and can possibly reduce lags or latency when a game has to load assets
 * on demand.
 */
void game_mode_apply_ioprio(const GameModeContext *self, const pid_t client)
{
	GameModeConfig *config = game_mode_config_from_context(self);

	LOG_MSG("Setting scheduling policies...\n");

	/*
	 * read configuration "ioprio" (0..7)
	 */
	int ioprio = 0;
	config_get_ioprio_value(config, &ioprio);
	if (IOPRIO_RESET_DEFAULT == ioprio) {
		LOG_MSG("IO priority will be reset to default behavior (based on CPU priority).\n");
		ioprio = 0;
	} else if (IOPRIO_DONT_SET == ioprio) {
		return;
	} else {
		/* maybe clamp the value */
		int invalid_ioprio = ioprio;
		ioprio = CLAMP(0, 7, ioprio);
		if (ioprio != invalid_ioprio)
			LOG_ONCE(ERROR,
			         "IO priority value %d invalid, clamping to %d\n",
			         invalid_ioprio,
			         ioprio);

		/* We support only IOPRIO_CLASS_BE as IOPRIO_CLASS_RT required CAP_SYS_ADMIN */
		ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, ioprio);
	}

	/*
	 * Actually apply the io priority
	 */
	int c = IOPRIO_PRIO_CLASS(ioprio), p = IOPRIO_PRIO_DATA(ioprio);
	if (ioprio_set(IOPRIO_WHO_PROCESS, client, ioprio) == 0) {
		if (0 == ioprio)
			LOG_MSG("Resetting client [%d] IO priority.\n", client);
		else
			LOG_MSG("Setting client [%d] IO priority to (%d,%d).\n", client, c, p);
	} else {
		LOG_ERROR("Setting client [%d] IO priority to (%d,%d) failed with error %d, ignoring\n",
		          client,
		          c,
		          p,
		          errno);
	}
}
