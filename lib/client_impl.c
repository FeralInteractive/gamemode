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

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

// Storage for error strings
static char error_string[512] = { 0 };

// Simple requestor function for a gamemode
static int gamemode_request(const char *function, int arg)
{
	sd_bus_message *msg = NULL;
	sd_bus *bus = NULL;

	int result = -1;

	// Open the user bus
	int ret = sd_bus_open_user(&bus);
	if (ret < 0) {
		snprintf(error_string,
		         sizeof(error_string),
		         "Could not connect to bus: %s",
		         strerror(-ret));
	} else {
		// Attempt to send the requested function
		ret = sd_bus_call_method(bus,
		                         "com.feralinteractive.GameMode",
		                         "/com/feralinteractive/GameMode",
		                         "com.feralinteractive.GameMode",
		                         function,
		                         NULL,
		                         &msg,
		                         arg ? "ii" : "i",
		                         getpid(),
		                         arg);
		if (ret < 0) {
			snprintf(error_string,
			         sizeof(error_string),
			         "Could not call method on bus: %s",
			         strerror(-ret));
		} else {
			// Read the reply
			ret = sd_bus_message_read(msg, "i", &result);
			if (ret < 0) {
				snprintf(error_string,
				         sizeof(error_string),
				         "Failure to parse response: %s",
				         strerror(-ret));
			}
		}
	}

	return result;
}

// Get the error string
extern const char *real_gamemode_error_string(void)
{
	return error_string;
}

// Wrapper to call RegisterGame
extern int real_gamemode_request_start(void)
{
	return gamemode_request("RegisterGame", 0);
}

// Wrapper to call UnregisterGame
extern int real_gamemode_request_end(void)
{
	return gamemode_request("UnregisterGame", 0);
}

// Wrapper to call QueryStatus
extern int real_gamemode_query_status(void)
{
	return gamemode_request("QueryStatus", 0);
}

// Wrapper to call RegisterGameByPID
extern int real_gamemode_register_start_for(pid_t pid)
{
	return gamemode_request("RegisterGameByPID", pid);
}

// Wrapper to call UnregisterGameByPID
extern int real_gamemode_register_end_for(pid_t pid)
{
	return gamemode_request("UnregisterGameByPID", pid);
}

// Wrapper to call QueryStatusFor
extern int real_gamemode_query_status_for(pid_t pid)
{
	return gamemode_request("QueryStatusFor", pid);
}
