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

	int status = game_mode_context_register(context, (pid_t)pid, (pid_t)pid);

	return sd_bus_reply_method_return(m, "i", status);
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

	int status = game_mode_context_unregister(context, (pid_t)pid, (pid_t)pid);

	return sd_bus_reply_method_return(m, "i", status);
}

/**
 * Handles the QueryStatus D-BUS Method
 */
static int method_query_status(sd_bus_message *m, void *userdata,
                               __attribute__((unused)) sd_bus_error *ret_error)
{
	int pid = 0;
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "i", &pid);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	int status = game_mode_context_query_status(context, (pid_t)pid, (pid_t)pid);

	return sd_bus_reply_method_return(m, "i", status);
}

/**
 * Handles the RegisterGameByPID D-BUS Method
 */
static int method_register_game_by_pid(sd_bus_message *m, void *userdata,
                                       __attribute__((unused)) sd_bus_error *ret_error)
{
	int callerpid = 0;
	int gamepid = 0;
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "ii", &callerpid, &gamepid);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	int reply = game_mode_context_register(context, (pid_t)gamepid, (pid_t)callerpid);

	return sd_bus_reply_method_return(m, "i", reply);
}

/**
 * Handles the UnregisterGameByPID D-BUS Method
 */
static int method_unregister_game_by_pid(sd_bus_message *m, void *userdata,
                                         __attribute__((unused)) sd_bus_error *ret_error)
{
	int callerpid = 0;
	int gamepid = 0;
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "ii", &callerpid, &gamepid);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	int reply = game_mode_context_unregister(context, (pid_t)gamepid, (pid_t)callerpid);

	return sd_bus_reply_method_return(m, "i", reply);
}

/**
 * Handles the QueryStatus D-BUS Method
 */
static int method_query_status_by_pid(sd_bus_message *m, void *userdata,
                                      __attribute__((unused)) sd_bus_error *ret_error)
{
	int callerpid = 0;
	int gamepid = 0;
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "ii", &callerpid, &gamepid);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	int status = game_mode_context_query_status(context, (pid_t)gamepid, (pid_t)callerpid);

	return sd_bus_reply_method_return(m, "i", status);
}
/**
 * Handles the Active D-BUS Method
 */
static int property_get_client_count(sd_bus *local_bus, const char *path, const char *interface,
                                     const char *property, sd_bus_message *reply, void *userdata,
                                     __attribute__((unused)) sd_bus_error *ret_error)
{
	GameModeContext *context = userdata;
	int count;

	count = game_mode_context_num_clients(context);

	return sd_bus_message_append_basic(reply, 'i', &count);
}

void game_mode_client_count_changed(void)
{
	(void)sd_bus_emit_properties_changed(bus,
	                                     "/com/feralinteractive/GameMode",
	                                     "com.feralinteractive.GameMode",
	                                     "ClientCount",
	                                     NULL);
}

/**
 * Handles the Refresh Config request
 */
static int method_refresh_config(sd_bus_message *m, void *userdata,
                                 __attribute__((unused)) sd_bus_error *ret_error)
{
	GameModeContext *context = userdata;
	int status = game_mode_reload_config(context);
	return sd_bus_reply_method_return(m, "i", status);
}

/**
 * D-BUS vtable to dispatch virtual methods
 */
/* clang-format off */
static const sd_bus_vtable gamemode_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("ClientCount", "i", property_get_client_count, 0,
	                SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_METHOD("RegisterGame", "i", "i", method_register_game, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("UnregisterGame", "i", "i", method_unregister_game, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("QueryStatus", "i", "i", method_query_status, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RegisterGameByPID", "ii", "i", method_register_game_by_pid,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("UnregisterGameByPID", "ii", "i", method_unregister_game_by_pid,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("QueryStatusByPID", "ii", "i", method_query_status_by_pid,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RefreshConfig", "", "i", method_refresh_config, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END
};
/* clang-format on */

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
		FATAL_ERROR("Failed to connect to the bus: %s\n", strerror(-ret));
	}

	/* Create the object to allow connections */
	ret = sd_bus_add_object_vtable(bus,
	                               &slot,
	                               "/com/feralinteractive/GameMode",
	                               "com.feralinteractive.GameMode",
	                               gamemode_vtable,
	                               context);

	if (ret < 0) {
		FATAL_ERROR("Failed to install GameMode object: %s\n", strerror(-ret));
	}

	/* Request our name */
	ret = sd_bus_request_name(bus, "com.feralinteractive.GameMode", 0);
	if (ret < 0) {
		FATAL_ERROR("Failed to acquire service name: %s\n", strerror(-ret));
	}

	LOG_MSG("Successfully initialised bus with name [%s]...\n", "com.feralinteractive.GameMode");
	sd_notifyf(0, "STATUS=%sGameMode is ready to be activated.%s\n", "\x1B[1;36m", "\x1B[0m");

	/* Now loop, waiting for callbacks */
	for (;;) {
		ret = sd_bus_process(bus, NULL);
		if (ret < 0) {
			FATAL_ERROR("Failure when processing the bus: %s\n", strerror(-ret));
		}

		/* We're done processing */
		if (ret > 0) {
			continue;
		}

		/* Wait for more */
		ret = sd_bus_wait(bus, (uint64_t)-1);
		if (ret < 0 && -ret != EINTR) {
			FATAL_ERROR("Failure when waiting on bus: %s\n", strerror(-ret));
		}
	}
}

/**
 * Attempts to inhibit the screensaver
 * Uses the "org.freedesktop.ScreenSaver" interface
 */
static unsigned int screensaver_inhibit_cookie = 0;
int game_mode_inhibit_screensaver(bool inhibit)
{
	const char *service = "org.freedesktop.ScreenSaver";
	const char *object_path = "/org/freedesktop/ScreenSaver";
	const char *interface = "org.freedesktop.ScreenSaver";
	const char *function = inhibit ? "Inhibit" : "UnInhibit";

	sd_bus_message *msg = NULL;
	sd_bus *bus_local = NULL;
	sd_bus_error err;
	memset(&err, 0, sizeof(sd_bus_error));

	int result = -1;

	// Open the user bus
	int ret = sd_bus_open_user(&bus_local);
	if (ret < 0) {
		LOG_ERROR("Could not connect to user bus: %s\n", strerror(-ret));
		return -1;
	}

	if (inhibit) {
		ret = sd_bus_call_method(bus_local,
		                         service,
		                         object_path,
		                         interface,
		                         function,
		                         &err,
		                         &msg,
		                         "ss",
		                         "com.feralinteractive.GameMode",
		                         "GameMode Activated");
	} else {
		ret = sd_bus_call_method(bus_local,
		                         service,
		                         object_path,
		                         interface,
		                         function,
		                         &err,
		                         &msg,
		                         "u",
		                         screensaver_inhibit_cookie);
	}

	if (ret < 0) {
		LOG_ERROR(
		    "Could not call %s on %s: %s\n"
		    "\t%s\n"
		    "\t%s\n",
		    function,
		    service,
		    strerror(-ret),
		    err.name,
		    err.message);
	} else if (inhibit) {
		// Read the reply
		ret = sd_bus_message_read(msg, "u", &screensaver_inhibit_cookie);
		if (ret < 0) {
			LOG_ERROR("Failure to parse response from %s on %s: %s\n",
			          function,
			          service,
			          strerror(-ret));
		} else {
			result = 0;
		}
	} else {
		result = 0;
	}

	sd_bus_unref(bus_local);

	return result;
}
