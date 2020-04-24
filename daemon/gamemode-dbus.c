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
#include "common-pidfds.h"

#ifdef USE_ELOGIND
#include <elogind/sd-bus.h>
#include <elogind/sd-daemon.h>
#else
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define GAME_PATH_PREFIX "/com/feralinteractive/GameMode/Games"
/* maximum length of a valid game object path string:
 *   The path prefix including \0 (sizeof), another '/', and 10 digits for uint32_t ('%u')*/
#define GAME_PATH_MAX (sizeof(GAME_PATH_PREFIX) + 11)

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
 * Handles the QueryStatusByPID D-BUS Method
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
 * Handles the RegisterGameByPIDFd D-BUS Method
 */
static int method_register_game_by_pidfd(sd_bus_message *m, void *userdata,
                                         __attribute__((unused)) sd_bus_error *ret_error)
{
	int fds[2] = { -1, -1 };
	pid_t pids[2] = { 0, 0 };
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "hh", &fds[0], &fds[1]);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	int reply = pidfds_to_pids(fds, pids, 2);

	if (reply == 2)
		reply = game_mode_context_register(context, pids[0], pids[1]);
	else
		reply = -1;

	return sd_bus_reply_method_return(m, "i", reply);
}

/**
 * Handles the UnregisterGameByPIDFd D-BUS Method
 */
static int method_unregister_game_by_pidfd(sd_bus_message *m, void *userdata,
                                           __attribute__((unused)) sd_bus_error *ret_error)
{
	int fds[2] = { -1, -1 };
	pid_t pids[2] = { 0, 0 };
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "hh", &fds[0], &fds[1]);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	int reply = pidfds_to_pids(fds, pids, 2);

	if (reply == 2)
		reply = game_mode_context_unregister(context, pids[0], pids[1]);
	else
		reply = -1;

	return sd_bus_reply_method_return(m, "i", reply);
}

/**
 * Handles the QueryStatusByPIDFd D-BUS Method
 */
static int method_query_status_by_pidfd(sd_bus_message *m, void *userdata,
                                        __attribute__((unused)) sd_bus_error *ret_error)
{
	int fds[2] = { -1, -1 };
	pid_t pids[2] = { 0, 0 };
	GameModeContext *context = userdata;

	int ret = sd_bus_message_read(m, "hh", &fds[0], &fds[1]);
	if (ret < 0) {
		LOG_ERROR("Failed to parse input parameters: %s\n", strerror(-ret));
		return ret;
	}

	int reply = pidfds_to_pids(fds, pids, 2);

	if (reply == 2)
		reply = game_mode_context_query_status(context, pids[0], pids[1]);
	else
		reply = -1;

	return sd_bus_reply_method_return(m, "i", reply);
}

/**
 * Handles the ClientCount D-BUS Property
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

static inline void game_object_bus_path(pid_t pid, char path[static GAME_PATH_MAX])
{
	snprintf(path, GAME_PATH_MAX, GAME_PATH_PREFIX "/%u", (uint32_t)pid);
}

/**
 * Handles the List Games
 */
static int method_list_games(sd_bus_message *m, void *userdata,
                             __attribute__((unused)) sd_bus_error *ret_error)
{
	GameModeContext *context = userdata;
	sd_bus_message *reply = NULL;
	unsigned int count;
	pid_t *clients;
	int r;

	r = sd_bus_message_new_method_return(m, &reply);
	if (r < 0)
		return r;

	r = sd_bus_message_open_container(reply, 'a', "(io)");
	if (r < 0)
		return r;

	clients = game_mode_context_list_clients(context, &count);

	for (unsigned int i = 0; i < count; i++) {
		char path[GAME_PATH_MAX] = {
			0,
		};
		pid_t pid = clients[i];

		game_object_bus_path(pid, path);
		r = sd_bus_message_append(reply, "(io)", (int32_t)pid, path);

		if (r < 0)
			break;
	}

	free(clients);

	if (r < 0)
		return r;

	r = sd_bus_message_close_container(reply);
	if (r < 0)
		return r;

	return sd_bus_send(NULL, reply, NULL);
}

/* Signal emission helper */
static void game_mode_client_send_game_signal(pid_t pid, bool new_game)
{
	char path[GAME_PATH_MAX] = {
		0,
	};
	int ret;

	game_object_bus_path(pid, path);
	ret = sd_bus_emit_signal(bus,
	                         "/com/feralinteractive/GameMode",
	                         "com.feralinteractive.GameMode",
	                         new_game ? "GameRegistered" : "GameUnregistered",
	                         "io",
	                         (int32_t)pid,
	                         path);
	if (ret < 0)
		fprintf(stderr, "failed to emit signal: %s", strerror(-ret));

	(void)sd_bus_emit_properties_changed(bus,
	                                     "/com/feralinteractive/GameMode",
	                                     "com.feralinteractive.GameMode",
	                                     "ClientCount",
	                                     NULL);
}

/* Emit GameRegistered signal */
void game_mode_client_registered(pid_t pid)
{
	game_mode_client_send_game_signal(pid, true);
}

/* Emit GameUnregistered signal */
void game_mode_client_unregistered(pid_t pid)
{
	game_mode_client_send_game_signal(pid, false);
}

/**
 * D-BUS vtable to dispatch virtual methods
 */
/* This bit seems to be formatted differently by different clang-format versions */
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
	SD_BUS_METHOD("RegisterGameByPIDFd", "hh", "i", method_register_game_by_pidfd,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("UnregisterGameByPIDFd", "hh", "i", method_unregister_game_by_pidfd,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("QueryStatusByPIDFd", "hh", "i", method_query_status_by_pidfd,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RefreshConfig", "", "i", method_refresh_config, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("ListGames", "", "a(io)", method_list_games, SD_BUS_VTABLE_UNPRIVILEGED),

	SD_BUS_SIGNAL("GameRegistered", "io", 0),
	SD_BUS_SIGNAL("GameUnregistered", "io", 0),

	SD_BUS_VTABLE_END
};

/**
 * Game Objects
 */

static inline void pid_to_pointer(pid_t pid, void **pointer)
{
	_Static_assert(sizeof (void *) >= sizeof (pid_t),
		       "pointer type not large enough to store pid_t");

	*pointer = (void *) (intptr_t) pid;
}

static inline pid_t pid_from_pointer(const void *pointer)
{
	return (pid_t) (intptr_t) pointer;
}

static int game_object_find(sd_bus *local_bus, const char *path, const char *interface,
			    void *userdata, void **found, sd_bus_error *ret_error)
{
	static const char prefix[] = GAME_PATH_PREFIX "/";
	const char *start;
	unsigned long int n;
	char *end;

	if (strncmp(path, prefix, strlen(prefix)) != 0)
		return 0;

	start = path + strlen(prefix);

	errno = 0;
	n = strtoul(start, &end, 10);

	if (start == end || errno != 0)
		return 0;

	pid_to_pointer((pid_t) n, found);

	return 1;
}

static int game_node_enumerator(sd_bus *local_bus, const char *path, void *userdata,
				char ***nodes,
				__attribute__((unused)) sd_bus_error *ret_error)
{
	GameModeContext *context = userdata;
	unsigned int count;
	pid_t *clients;
	char **strv = NULL;

	clients = game_mode_context_list_clients(context, &count);

	strv = malloc (sizeof (char *) * (count + 1));

	for (unsigned int i = 0; i < count; i++) {
		char bus_path[GAME_PATH_MAX] = {0, };

		game_object_bus_path(clients[i], bus_path);
		strv[i] = strdup (bus_path);
	}

	strv[count] = NULL;
	*nodes = strv;

	free(clients);

	return 1;
}

/**
 * Handles the ProcessId property for Game objects
 */
static int game_object_get_process_id(sd_bus *local_bus, const char *path, const char *interface,
				      const char *property, sd_bus_message *reply, void *userdata,
				      sd_bus_error *ret_error)
{
	GameModeClient *client;
	GameModeContext *context;
	pid_t pid;
	int pv;
	int ret;

	pid = pid_from_pointer(userdata);
	context = game_mode_context_instance();
	client = game_mode_context_lookup_client(context, pid);

	pv = (int) pid;

	if (client == NULL) {
		return sd_bus_error_setf(ret_error,
					 SD_BUS_ERROR_UNKNOWN_OBJECT,
					 "No client registered with id '%d'", pv);
	}

	ret = sd_bus_message_append_basic(reply, 'i', &pv);
	game_mode_client_unref(client);

	return ret;
}

/**
 * Handles the Exectuable property for Game objects
 */
static int game_object_get_executable(sd_bus *local_bus, const char *path, const char *interface,
				      const char *property, sd_bus_message *reply, void *userdata,
				      sd_bus_error *ret_error)
{
	GameModeClient *client;
	GameModeContext *context;
	const char *exec;
	pid_t pid;
	int ret;

	pid = pid_from_pointer(userdata);

	context = game_mode_context_instance();
	client = game_mode_context_lookup_client(context, pid);

	if (client == NULL) {
		return sd_bus_error_setf(ret_error,
					 SD_BUS_ERROR_UNKNOWN_OBJECT,
					 "No client registered with id '%d'", (int) pid);
	}

	exec = game_mode_client_get_executable(client);
	ret = sd_bus_message_append_basic(reply, 's', exec);
	game_mode_client_unref(client);

	return ret;
}

/**
 * Handles the Requester property for Game objects
 */
static int game_object_get_requester(sd_bus *local_bus, const char *path, const char *interface,
				     const char *property, sd_bus_message *reply, void *userdata,
				     sd_bus_error *ret_error)
{
	GameModeClient *client;
	GameModeContext *context;
	pid_t requester;
	pid_t pid;
	int ret;
	int pv;

	pid = pid_from_pointer(userdata);

	context = game_mode_context_instance();
	client = game_mode_context_lookup_client(context, pid);

	if (client == NULL) {
		return sd_bus_error_setf(ret_error,
					 SD_BUS_ERROR_UNKNOWN_OBJECT,
					 "No client registered with id '%d'", (int) pid);
	}

	requester = game_mode_client_get_requester(client);
	pv = (int) requester;

	ret = sd_bus_message_append_basic(reply, 'i', &pv);
	game_mode_client_unref(client);

	return ret;
}
/**
 * Handles the Timestamp property for Game objects
 */
static int game_object_get_timestamp(sd_bus *local_bus, const char *path, const char *interface,
				     const char *property, sd_bus_message *reply, void *userdata,
				     sd_bus_error *ret_error)
{
	GameModeClient *client;
	GameModeContext *context;
	uint64_t timestamp;
	pid_t pid;
	int ret;

	pid = pid_from_pointer(userdata);

	context = game_mode_context_instance();
	client = game_mode_context_lookup_client(context, pid);

	if (client == NULL) {
		return sd_bus_error_setf(ret_error,
					 SD_BUS_ERROR_UNKNOWN_OBJECT,
					 "No client registered with id '%d'", (int) pid);
	}

	timestamp = game_mode_client_get_timestamp(client);
	ret = sd_bus_message_append_basic(reply, 't', &timestamp);
	game_mode_client_unref(client);

	return ret;
}

/* Same as above: this bit seems to be formatted differently by different clang-format versions */
/* clang-format off */
static const sd_bus_vtable game_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("ProcessId", "i", game_object_get_process_id, 0,
	                SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Executable", "s", game_object_get_executable, 0,
	                SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Requester", "i", game_object_get_requester, 0,
	                SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Timestamp", "t", game_object_get_timestamp, 0,
	                SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
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

	ret = sd_bus_add_fallback_vtable(bus,
	                                 &slot,
	                                 GAME_PATH_PREFIX,
	                                 "com.feralinteractive.GameMode.Game",
	                                 game_vtable,
	                                 game_object_find,
	                                 context);

	if (ret < 0) {
		FATAL_ERROR("Failed to install Game object: %s\n", strerror(-ret));
	}

	ret = sd_bus_add_node_enumerator(bus, &slot, GAME_PATH_PREFIX, game_node_enumerator, context);
	if (ret < 0) {
		FATAL_ERROR("Failed to install Game object enumerator: %s\n", strerror(-ret));
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
