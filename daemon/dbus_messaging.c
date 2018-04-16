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

#include "dbus_messaging.h"
#include "daemonize.h"
#include "gamemode.h"
#include "governors.h"
#include "logging.h"

#include <stdlib.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>

/* systemd dbus components */
static sd_bus *bus = NULL;
static sd_bus_slot *slot = NULL;

/**
 * Clean up our private dbus state
 */
static void clean_up(void)
{
	if (slot) {
		sd_bus_slot_unref(slot);
	}
	slot = NULL;
	if (bus) {
		sd_bus_unref(bus);
	}
	bus = NULL;
}

/**
 * Handles the RegisterGame D-BUS Method
 */
static int method_register_game(sd_bus_message *m, void *userdata,
                                __attribute__((unused)) sd_bus_error *ret_error)
{
	int pid = 0;
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "i", &pid);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	game_mode_context_register(context, (pid_t)pid);

	return sd_bus_reply_method_return(m, "i", 0);
}

/**
 * Handles the UnregisterGame D-BUS Method
 */
static int method_unregister_game(sd_bus_message *m, void *userdata,
                                  __attribute__((unused)) sd_bus_error *ret_error)
{
	int pid = 0;
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "i", &pid);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	game_mode_context_unregister(context, (pid_t)pid);

	return sd_bus_reply_method_return(m, "i", 0);
}

/**
 * D-BUS vtable to dispatch virtual methods
 */
static const sd_bus_vtable gamemode_vtable[] =
    { SD_BUS_VTABLE_START(0),
	  SD_BUS_METHOD("RegisterGame", "i", "i", method_register_game, SD_BUS_VTABLE_UNPRIVILEGED),
	  SD_BUS_METHOD("UnregisterGame", "i", "i", method_unregister_game, SD_BUS_VTABLE_UNPRIVILEGED),
	  SD_BUS_VTABLE_END };

/**
 * Main process loop for the daemon. Run until quitting has been requested.
 */
void game_mode_context_loop(GameModeContext *context)
{
	/* Set up function to handle clean up of resources */
	atexit(clean_up);
	int ret = 0;

	/* Connect to the session bus */
	ret = sd_bus_open_user(&bus);

	if (ret < 0) {
		FATAL_ERROR("Failed to connect to the bus: %s", strerror(-ret));
	}

	/* Create the object to allow connections */
	ret = sd_bus_add_object_vtable(bus,
	                               &slot,
	                               "/com/feralinteractive/GameMode",
	                               "com.feralinteractive.GameMode",
	                               gamemode_vtable,
	                               context);

	if (ret < 0) {
		FATAL_ERROR("Failed to install GameMode object: %s", strerror(-ret));
	}

	/* Request our name */
	ret = sd_bus_request_name(bus, "com.feralinteractive.GameMode", 0);
	if (ret < 0) {
		FATAL_ERROR("Failed to acquire service name: %s", strerror(-ret));
	}

	LOG_MSG("Successfully initialised bus with name [%s]...\n", "com.feralinteractive.GameMode");
	sd_notifyf(0,"STATUS=%sGameMode is ready to be activated.%s\n", "\x1B[1;36m", "\x1B[0m");

	/* Now loop, waiting for callbacks */
	for (;;) {
		ret = sd_bus_process(bus, NULL);
		if (ret < 0) {
			FATAL_ERROR("Failure when processing the bus: %s", strerror(-ret));
		}

		/* We're done processing */
		if (ret > 0) {
			continue;
		}

		/* Wait for more */
		ret = sd_bus_wait(bus, (uint64_t)-1);
		if (ret < 0 && -ret != EINTR) {
			FATAL_ERROR("Failure when waiting on bus: %s", strerror(-ret));
		}
	}
}
