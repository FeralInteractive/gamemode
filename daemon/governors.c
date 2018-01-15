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
#include "governors.h"
#include "logging.h"

#include <linux/limits.h>
#include <stdio.h>
#include <unistd.h>

static char initial[32];

// Store the initial governor state to be referenced later
void update_initial_gov_state()
{
	static char *command = "cpugovctl get";

	FILE *f = popen(command, "r");
	if (!f) {
		FATAL_ERRORNO("Failed to launch \"%s\" script", command);
	}

	if (!fgets(initial, sizeof(initial) - 1, f)) {
		FATAL_ERROR("Failed to get output from \"%s\"", command);
	}

	pclose(f);

	strtok(initial, "\n");
}

// Sets all governors to a value, if NULL argument provided, will reset them back
void set_governors(const char *value)
{
	const char *newval = value ? value : initial;
	LOG_MSG("Setting governors to %s\n", newval ? newval : "initial values");

	char command[PATH_MAX] = {};
	snprintf(command, sizeof(command), "cpugovctl set %s", newval);

	FILE *f = popen(command, "r");
	if (!f) {
		FATAL_ERRORNO("Failed to launch %s script", command);
	}

	pclose(f);
}

// Return the initial governor
const char *get_initial_governor()
{
	return initial;
}
