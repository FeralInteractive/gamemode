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

#include <common-helpers.h>
#include <common-pidfds.h>

#include <dbus/dbus.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// For developmental purposes
#define DO_TRACE 0

// D-Bus name, path, iface
#define DAEMON_DBUS_NAME "com.feralinteractive.GameMode"
#define DAEMON_DBUS_PATH "/com/feralinteractive/GameMode"
#define DAEMON_DBUS_IFACE "com.feralinteractive.GameMode"

#define PORTAL_DBUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_DBUS_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_DBUS_IFACE "org.freedesktop.portal.GameMode"

// Cleanup macros
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_bus_ _cleanup_(hop_off_the_bus)
#define _cleanup_msg_ _cleanup_(cleanup_msg)
#define _cleanup_dpc_ _cleanup_(cleanup_pending_call)
#define _cleanup_fds_ _cleanup_(cleanup_fd_array)

#ifdef NDEBUG
#define DEBUG(...)
#else
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#endif

#if DO_TRACE
#define TRACE(...) fprintf(stderr, __VA_ARGS__)
#else
#define TRACE(...)
#endif

// Prototypes
static int log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Storage for error strings
static char error_string[512] = { 0 };

// memory helpers
static void cleanup_fd_array(int **fdlist)
{
	if (fdlist == NULL || *fdlist == NULL)
		return;

	int errsave = errno;
	for (int *fd = *fdlist; *fd != -1; fd++) {
		TRACE("GM Closing fd %d\n", *fd);
		(void)close(*fd);
	}

	errno = errsave;
	free(*fdlist);
}

// Allocate a -1 termianted array of ints
static inline int *alloc_fd_array(int n)
{
	int *fds;

	size_t count = (size_t)n + 1; /* -1, terminated */
	fds = (int *)malloc(sizeof(int) * count);
	for (size_t i = 0; i < count; i++)
		fds[i] = -1;

	return fds;
}

// Helper to check if we are running inside a sandboxed framework like Flatpak or Snap
static int in_sandbox(void)
{
	static int status = -1;

	if (status == -1) {
		struct stat sb;
		int r;

		r = lstat("/.flatpak-info", &sb);
		status = r == 0 && sb.st_size > 0;

		if (getenv("SNAP") != NULL) {
			status = 1;
		}
	}

	return status;
}

static int log_error(const char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsnprintf(error_string, sizeof(error_string), fmt, args);
	va_end(args);

	if (n < 0)
		DEBUG("Failed to format error string");
	else if ((size_t)n >= sizeof(error_string))
		DEBUG("Error log overflow");

	fprintf(stderr, "GameMode ERROR: %s\n", error_string);

	return -1;
}

static void hop_off_the_bus(DBusConnection **bus)
{
	if (bus == NULL || *bus == NULL)
		return;

	dbus_connection_unref(*bus);
}

static DBusConnection *hop_on_the_bus(void)
{
	DBusConnection *bus;
	DBusError err;

	dbus_error_init(&err);

	bus = dbus_bus_get(DBUS_BUS_SESSION, &err);

	if (bus == NULL) {
		log_error("Could not connect to bus: %s", err.message);
		dbus_error_free(&err);
	}

	return bus;
}

/* cleanup functions */
static void cleanup_msg(DBusMessage **msg)
{
	if (msg == NULL || *msg == NULL)
		return;

	dbus_message_unref(*msg);
}

static void cleanup_pending_call(DBusPendingCall **call)
{
	if (call == NULL || *call == NULL)
		return;

	dbus_pending_call_unref(*call);
}

/* internal API */
static int make_request(DBusConnection *bus, int native, int use_pidfds, const char *method,
                        pid_t *pids, int npids, DBusError *error)
{
	_cleanup_msg_ DBusMessage *msg = NULL;
	_cleanup_dpc_ DBusPendingCall *call = NULL;
	_cleanup_fds_ int *fds = NULL;
	char action[256] = {
		0,
	};
	DBusError err;
	DBusMessageIter iter;
	int res = -1;

	TRACE("GM: Incoming request: %s, npids: %d, native: %d pifds: %d\n",
	      method,
	      npids,
	      native,
	      use_pidfds);

	if (use_pidfds) {
		fds = alloc_fd_array(npids);

		res = open_pidfds(pids, fds, npids);
		if (res != npids) {
			dbus_set_error(error, DBUS_ERROR_FAILED, "Could not open pidfd for %d", (int)pids[res]);
			return -1;
		}

		if (strstr(method, "ByPID"))
			snprintf(action, sizeof(action), "%sFd", method);
		else
			snprintf(action, sizeof(action), "%sByPIDFd", method);
		method = action;
	}

	TRACE("GM:   Making request: %s, npids: %d, native: %d pifds: %d\n",
	      method,
	      npids,
	      native,
	      use_pidfds);

	// If we are inside a Flatpak or Snap we need to talk to the portal instead
	const char *dest = native ? DAEMON_DBUS_NAME : PORTAL_DBUS_NAME;
	const char *path = native ? DAEMON_DBUS_PATH : PORTAL_DBUS_PATH;
	const char *iface = native ? DAEMON_DBUS_IFACE : PORTAL_DBUS_IFACE;

	msg = dbus_message_new_method_call(dest, path, iface, method);

	if (!msg) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED, "Could not create dbus message");
		return -1;
	}

	dbus_message_iter_init_append(msg, &iter);

	for (int i = 0; i < npids; i++) {
		dbus_int32_t p;
		int type;

		if (use_pidfds) {
			type = DBUS_TYPE_UNIX_FD;
			p = (dbus_int32_t)fds[i];
		} else {
			type = DBUS_TYPE_INT32;
			p = (dbus_int32_t)pids[i];
		}
		dbus_message_iter_append_basic(&iter, type, &p);
	}

	dbus_connection_send_with_reply(bus, msg, &call, -1);
	dbus_connection_flush(bus);
	dbus_message_unref(msg);
	msg = NULL;

	dbus_pending_call_block(call);
	msg = dbus_pending_call_steal_reply(call);

	if (msg == NULL) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED, "Did not receive a reply");
		return -1;
	}

	dbus_error_init(&err);
	res = -1;
	if (dbus_set_error_from_message(&err, msg)) {
		dbus_set_error(error,
		               err.name,
		               "Could not call method '%s' on '%s': %s",
		               method,
		               dest,
		               err.message);
	} else if (!dbus_message_iter_init(msg, &iter) ||
	           dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
		dbus_set_error(error, DBUS_ERROR_INVALID_SIGNATURE, "Failed to parse response");
	} else {
		dbus_message_iter_get_basic(&iter, &res);
	}

	/* free the local error */
	if (dbus_error_is_set(&err))
		dbus_error_free(&err);

	return res;
}

static int gamemode_request(const char *method, pid_t for_pid)
{
	_cleanup_bus_ DBusConnection *bus = NULL;
	static int use_pidfs = 1;
	DBusError err;
	pid_t pids[2];
	int npids;
	int native;
	int res = -1;

	native = !in_sandbox();

	/* pid[0] is the client, i.e. the game
	 * pid[1] is the requestor, i.e. this process
	 *
	 * we setup the array such that pids[1] will always be a valid
	 * pid, because if we are going to use the pidfd based API,
	 * both pids are being sent, even if they are the same
	 */
	pids[1] = getpid();
	pids[0] = for_pid != 0 ? for_pid : pids[1];

	TRACE("GM: [%d] request '%s' received (by: %d) [portal: %s]\n",
	      (int)pids[0],
	      method,
	      (int)pids[1],
	      (native ? "n" : "y"));

	bus = hop_on_the_bus();

	if (bus == NULL)
		return -1;

	dbus_error_init(&err);
retry:
	if (for_pid != 0 || use_pidfs)
		npids = 2;
	else
		npids = 1;

	res = make_request(bus, native, use_pidfs, method, pids, npids, &err);

	if (res == -1 && use_pidfs && dbus_error_is_set(&err)) {
		TRACE("GM: Request with pidfds failed (%s). Retrying.\n", err.message);
		use_pidfs = 0;
		dbus_error_free(&err);
		goto retry;
	}

	if (res == -1 && dbus_error_is_set(&err))
		log_error("D-Bus error: %s", err.message);

	TRACE("GM: [%d] request '%s' done: %d\n", (int)pids[0], method, res);

	if (dbus_error_is_set(&err))
		dbus_error_free(&err);

	return res;
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
extern int real_gamemode_request_start_for(pid_t pid)
{
	return gamemode_request("RegisterGameByPID", pid);
}

// Wrapper to call UnregisterGameByPID
extern int real_gamemode_request_end_for(pid_t pid)
{
	return gamemode_request("UnregisterGameByPID", pid);
}

// Wrapper to call QueryStatusByPID
extern int real_gamemode_query_status_for(pid_t pid)
{
	return gamemode_request("QueryStatusByPID", pid);
}
