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

#include "gamemode.h"
#include "daemon_config.h"
#include "governors-query.h"
#include "governors.h"
#include "ioprio.h"
#include "logging.h"

#include <ctype.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <linux/sched.h>
#include <pthread.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <systemd/sd-daemon.h>

/* SCHED_ISO may not be defined as it is a reserved value not yet
 * implemented in official kernel sources, see linux/sched.h.
 */
#ifndef SCHED_ISO
#define SCHED_ISO 4
#endif

/* Priority to renice the process to.
 */
#define NICE_DEFAULT_PRIORITY -4

/* Value clamping helper.
 */
#define CLAMP(lbound, ubound, value) MIN(MIN(lbound, ubound), MAX(MAX(lbound, ubound), value))

/* Little helper to safely print into a buffer, returns a newly allocated string
 */
#define safe_snprintf(b, s, ...)                                                                   \
	(snprintf(b, sizeof(b), s, __VA_ARGS__) < (ssize_t)sizeof(b) ? strndup(b, sizeof(b)) : NULL)

/**
 * Helper function: Test, if haystack ends with needle.
 */
static inline const char *strtail(const char *haystack, const char *needle)
{
	char *pos = strstr(haystack, needle);
	if (pos && (strlen(pos) == strlen(needle)))
		return pos;
	return NULL;
}

/**
 * The GameModeClient encapsulates the remote connection, providing a list
 * form to contain the pid and credentials.
 */
typedef struct GameModeClient {
	pid_t pid;                   /**< Process ID */
	struct GameModeClient *next; /**<Next client in the list */
	char *executable;            /**<Process executable */
} GameModeClient;

struct GameModeContext {
	pthread_rwlock_t rwlock; /**<Guard access to the client list */
	_Atomic int refcount;    /**<Allow cycling the game mode */
	GameModeClient *client;  /**<Pointer to first client */

	GameModeConfig *config; /**<Pointer to config object */

	char initial_cpu_mode[64]; /**<Only updates when we can */

	/* Reaper control */
	struct {
		pthread_t thread;
		bool running;
		pthread_mutex_t mutex;
		pthread_cond_t condition;
	} reaper;
};

static GameModeContext instance = { 0 };

/* Maximum number of concurrent processes we'll sanely support */
#define MAX_GAMES 256

/**
 * Protect against signals
 */
static volatile bool had_context_init = false;

static GameModeClient *game_mode_client_new(pid_t pid, char *exe);
static void game_mode_client_free(GameModeClient *client);
static bool game_mode_context_has_client(GameModeContext *self, pid_t client);
static int game_mode_context_num_clients(GameModeContext *self);
static void *game_mode_context_reaper(void *userdata);
static void game_mode_context_enter(GameModeContext *self);
static void game_mode_context_leave(GameModeContext *self);
static char *game_mode_context_find_exe(pid_t pid);

void game_mode_context_init(GameModeContext *self)
{
	if (had_context_init) {
		LOG_ERROR("Context already initialised\n");
		return;
	}
	had_context_init = true;
	self->refcount = ATOMIC_VAR_INIT(0);

	/* clear the initial string */
	memset(self->initial_cpu_mode, 0, sizeof(self->initial_cpu_mode));

	/* Initialise the config */
	self->config = config_create();
	config_init(self->config);

	pthread_rwlock_init(&self->rwlock, NULL);
	pthread_mutex_init(&self->reaper.mutex, NULL);
	pthread_cond_init(&self->reaper.condition, NULL);

	/* Get the reaper thread going */
	self->reaper.running = true;
	if (pthread_create(&self->reaper.thread, NULL, game_mode_context_reaper, self) != 0) {
		FATAL_ERROR("Couldn't construct a new thread");
	}
}

void game_mode_context_destroy(GameModeContext *self)
{
	if (!had_context_init) {
		return;
	}

	/* Leave game mode now */
	if (game_mode_context_num_clients(self) > 0) {
		game_mode_context_leave(self);
	}

	had_context_init = false;
	game_mode_client_free(self->client);
	self->reaper.running = false;

	/* We might be stuck waiting, so wake it up again */
	pthread_mutex_lock(&self->reaper.mutex);
	pthread_cond_signal(&self->reaper.condition);
	pthread_mutex_unlock(&self->reaper.mutex);

	/* Join the thread as soon as possible */
	pthread_join(self->reaper.thread, NULL);

	pthread_cond_destroy(&self->reaper.condition);
	pthread_mutex_destroy(&self->reaper.mutex);

	/* Destroy the config object */
	config_destroy(self->config);

	pthread_rwlock_destroy(&self->rwlock);
}

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
static void game_mode_apply_scheduler(GameModeContext *self, pid_t client)
{
	LOG_MSG("Setting scheduling policies...\n");

	/*
	 * read configuration "renice" (1..20)
	 */
	long int renice = 0;
	config_get_renice_value(self->config, &renice);
	if ((renice < 1) || (renice > 20)) {
		LOG_ERROR("Renice value [%ld] defaulted to [%d].\n", renice, -NICE_DEFAULT_PRIORITY);
		renice = NICE_DEFAULT_PRIORITY;
	} else {
		renice = -renice;
	}

	/*
	 * don't adjust priority if it was already adjusted
	 */
	if (getpriority(PRIO_PROCESS, (id_t)client) != 0) {
		LOG_ERROR("Client [%d] already reniced, ignoring.\n", client);
	} else if (setpriority(PRIO_PROCESS, (id_t)client, (int)renice)) {
		LOG_ERROR(
		    "Renicing client [%d] failed with error %d, ignoring (your user may not have "
		    "permission to do this).\n",
		    client,
		    errno);
	}

	/*
	 * read configuration "softrealtime" (on, off, auto)
	 */
	char softrealtime[CONFIG_VALUE_MAX] = { 0 };
	config_get_soft_realtime(self->config, softrealtime);

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
			LOG_ERROR(
			    "Setting client [%d] to SCHED_ISO failed with error %d, ignoring (your "
			    "kernel may not support this).\n",
			    client,
			    errno);
		}
	} else {
		LOG_ERROR("Not using softrealtime, setting is '%s'.\n", softrealtime);
	}
}

/**
 * Apply io priorities
 *
 * This tries to change the io priority of the client to a value specified
 * and can possibly reduce lags or latency when a game has to load assets
 * on demand.
 */
static void game_mode_apply_ioprio(GameModeContext *self, pid_t client)
{
	LOG_MSG("Setting scheduling policies...\n");

	/*
	 * read configuration "ioprio" (0..7)
	 */
	int ioprio = 0;
	config_get_ioprio_value(self->config, &ioprio);
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
			LOG_ERROR("IO priority value %d invalid, clamping to %d\n", invalid_ioprio, ioprio);

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

/**
 * Pivot into game mode.
 *
 * This is only possible after game_mode_context_init has made a GameModeContext
 * usable, and should always be followed by a game_mode_context_leave.
 */
static void game_mode_context_enter(GameModeContext *self)
{
	LOG_MSG("Entering Game Mode...\n");
	sd_notifyf(0, "STATUS=%sGameMode is now active.%s\n", "\x1B[1;32m", "\x1B[0m");

	char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	memset(scripts, 0, sizeof(scripts));
	config_get_gamemode_start_scripts(self->config, scripts);

	unsigned int i = 0;
	while (*scripts[i] != '\0' && i < CONFIG_LIST_MAX) {
		LOG_MSG("Executing script [%s]\n", scripts[i]);
		int err;
		if ((err = system(scripts[i])) != 0) {
			/* Log the failure, but this is not fatal */
			LOG_ERROR("Script [%s] failed with error %d\n", scripts[i], err);
		}
		i++;
	}

	/* Read the initial governor state so we can revert it correctly */
	const char *initial_state = get_gov_state();
	if (initial_state) {
		/* store the initial cpu governor mode */
		strncpy(self->initial_cpu_mode, initial_state, sizeof(self->initial_cpu_mode) - 1);
		self->initial_cpu_mode[sizeof(self->initial_cpu_mode) - 1] = '\0';
		LOG_MSG("governor was initially set to [%s]\n", initial_state);

		/* Choose the desired governor */
		char desired[CONFIG_VALUE_MAX] = { 0 };
		config_get_desired_governor(self->config, desired);
		const char *desiredGov = desired[0] != '\0' ? desired : "performance";

		/* set the governor to performance */
		if (!set_governors(desiredGov)) {
			/* if the set fails, clear the initial mode so we don't try and reset it back and fail
			 * again, presumably */
			memset(self->initial_cpu_mode, 0, sizeof(self->initial_cpu_mode));
		}
	}
}

/**
 * Pivot out of game mode.
 *
 * Should only be called after both init and game_mode_context_enter have
 * been performed.
 */
static void game_mode_context_leave(GameModeContext *self)
{
	LOG_MSG("Leaving Game Mode...\n");
	sd_notifyf(0, "STATUS=%sGameMode is currently deactivated.%s\n", "\x1B[1;36m", "\x1B[0m");

	/* Reset the governer state back to initial */
	if (self->initial_cpu_mode[0] != '\0') {
		/* Choose the governor to reset to, using the config to override */
		char defaultgov[CONFIG_VALUE_MAX] = { 0 };
		config_get_default_governor(self->config, defaultgov);
		const char *gov_mode = defaultgov[0] != '\0' ? defaultgov : self->initial_cpu_mode;

		set_governors(gov_mode);
		memset(self->initial_cpu_mode, 0, sizeof(self->initial_cpu_mode));
	}

	char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	memset(scripts, 0, sizeof(scripts));
	config_get_gamemode_end_scripts(self->config, scripts);

	unsigned int i = 0;
	while (*scripts[i] != '\0' && i < CONFIG_LIST_MAX) {
		LOG_MSG("Executing script [%s]\n", scripts[i]);
		int err;
		if ((err = system(scripts[i])) != 0) {
			/* Log the failure, but this is not fatal */
			LOG_ERROR("Script [%s] failed with error %d\n", scripts[i], err);
		}
		i++;
	}
}

/**
 * Automatically expire all dead processes
 *
 * This has to take special care to ensure thread safety and ensuring that our
 * pointer is never cached incorrectly.
 */
static void game_mode_context_auto_expire(GameModeContext *self)
{
	bool removing = true;

	while (removing) {
		pthread_rwlock_rdlock(&self->rwlock);
		removing = false;

		/* Each time we hit an expired game, start the loop back */
		for (GameModeClient *client = self->client; client; client = client->next) {
			if (kill(client->pid, 0) != 0) {
				LOG_MSG("Removing expired game [%i]...\n", client->pid);
				pthread_rwlock_unlock(&self->rwlock);
				game_mode_context_unregister(self, client->pid);
				removing = true;
				break;
			}
		}

		if (!removing) {
			pthread_rwlock_unlock(&self->rwlock);
			break;
		}
	}
}

/**
 * Determine if the client is already known to the context
 */
static bool game_mode_context_has_client(GameModeContext *self, pid_t client)
{
	bool found = false;
	pthread_rwlock_rdlock(&self->rwlock);

	/* Walk all clients and find a matching pid */
	for (GameModeClient *cl = self->client; cl; cl = cl->next) {
		if (cl->pid == client) {
			found = true;
			break;
		}
	}

	pthread_rwlock_unlock(&self->rwlock);
	return found;
}

/**
 * Helper to grab the current number of clients we know about
 */
static int game_mode_context_num_clients(GameModeContext *self)
{
	return atomic_load(&self->refcount);
}

bool game_mode_context_register(GameModeContext *self, pid_t client)
{
	/* Construct a new client if we can */
	GameModeClient *cl = NULL;
	char *executable = NULL;

	/* Cap the total number of active clients */
	if (game_mode_context_num_clients(self) + 1 > MAX_GAMES) {
		LOG_ERROR("Max games (%d) reached, not registering %d\n", MAX_GAMES, client);
		return false;
	}

	errno = 0;

	/* Check the PID first to spare a potentially expensive lookup for the exe */
	if (game_mode_context_has_client(self, client)) {
		LOG_ERROR("Addition requested for already known process [%d]\n", client);
		goto error_cleanup;
	}

	/* Lookup the executable first */
	executable = game_mode_context_find_exe(client);
	if (!executable)
		goto error_cleanup;

	/* Check our blacklist and whitelist */
	if (!config_get_client_whitelisted(self->config, executable)) {
		LOG_MSG("Client [%s] was rejected (not in whitelist)\n", executable);
		goto error_cleanup;
	} else if (config_get_client_blacklisted(self->config, executable)) {
		LOG_MSG("Client [%s] was rejected (in blacklist)\n", executable);
		goto error_cleanup;
	}

	/* From now on we depend on the client, initialize it */
	cl = game_mode_client_new(client, executable);
	if (cl)
		executable = NULL; // ownership has been delegated
	else
		goto error_cleanup;

	/* Begin a write lock now to insert our new client at list start */
	pthread_rwlock_wrlock(&self->rwlock);

	LOG_MSG("Adding game: %d [%s]\n", client, cl->executable);

	/* Update the list */
	cl->next = self->client;
	self->client = cl;
	pthread_rwlock_unlock(&self->rwlock);

	/* First add, init */
	if (atomic_fetch_add_explicit(&self->refcount, 1, memory_order_seq_cst) == 0) {
		game_mode_context_enter(self);
	}

	/* Apply scheduler policies */
	game_mode_apply_scheduler(self, client);

	/* Apply io priorities */
	game_mode_apply_ioprio(self, client);

	return true;

error_cleanup:
	if (errno != 0)
		LOG_ERROR("Failed to register client [%d]: %s\n", client, strerror(errno));
	free(executable);
	game_mode_client_free(cl);
	return false;
}

bool game_mode_context_unregister(GameModeContext *self, pid_t client)
{
	GameModeClient *cl = NULL;
	GameModeClient *prev = NULL;
	bool found = false;

	/* Requires locking. */
	pthread_rwlock_wrlock(&self->rwlock);

	for (prev = cl = self->client; cl; cl = cl->next) {
		if (cl->pid != client) {
			prev = cl;
			continue;
		}

		LOG_MSG("Removing game: %d [%s]\n", client, cl->executable);

		/* Found it */
		found = true;
		prev->next = cl->next;
		if (cl == self->client) {
			self->client = cl->next;
		}
		cl->next = NULL;
		game_mode_client_free(cl);
		break;
	}

	/* Unlock here, potentially yielding */
	pthread_rwlock_unlock(&self->rwlock);

	if (!found) {
		LOG_ERROR("Removal requested for unknown process [%d]\n", client);
		return false;
	}

	/* When we hit bottom then end the game mode */
	if (atomic_fetch_sub_explicit(&self->refcount, 1, memory_order_seq_cst) == 1) {
		game_mode_context_leave(self);
	}

	return true;
}

int game_mode_context_query_status(GameModeContext *self, pid_t client)
{
	GameModeClient *cl = NULL;
	int ret = 0;

	/*
	 * Check the current refcount on gamemode, this equates to whether gamemode is active or not,
	 * see game_mode_context_register and game_mode_context_unregister
	 */
	if (atomic_load_explicit(&self->refcount, memory_order_seq_cst)) {
		ret++;

		/* Check if the current client is registered */

		/* Requires locking. */
		pthread_rwlock_rdlock(&self->rwlock);

		for (cl = self->client; cl; cl = cl->next) {
			if (cl->pid != client) {
				continue;
			}

			/* Found it */
			ret++;
			break;
		}

		/* Unlock here, potentially yielding */
		pthread_rwlock_unlock(&self->rwlock);
	}

	return ret;
}

/**
 * Construct a new GameModeClient for the given process ID
 *
 * This is deliberately OOM safe
 */
static GameModeClient *game_mode_client_new(pid_t pid, char *executable)
{
	GameModeClient c = {
		.executable = executable,
		.next = NULL,
		.pid = pid,
	};
	GameModeClient *ret = NULL;

	ret = calloc(1, sizeof(struct GameModeClient));
	if (!ret) {
		return NULL;
	}
	*ret = c;
	return ret;
}

/**
 * Free a client and the next element in the list.
 */
static void game_mode_client_free(GameModeClient *client)
{
	if (!client) {
		return;
	}
	if (client->next) {
		game_mode_client_free(client->next);
	}
	if (client->executable) {
		free(client->executable);
	}
	free(client);
}

/**
 * We continuously run until told otherwise.
 */
static void *game_mode_context_reaper(void *userdata)
{
	/* Stack, not allocated, won't disappear. */
	GameModeContext *self = userdata;

	long reaper_interval = 0.0f;
	config_get_reaper_thread_frequency(self->config, &reaper_interval);

	struct timespec ts = { 0, 0 };
	ts.tv_sec = time(NULL) + reaper_interval;

	while (self->reaper.running) {
		/* Wait for condition */
		pthread_mutex_lock(&self->reaper.mutex);
		pthread_cond_timedwait(&self->reaper.condition, &self->reaper.mutex, &ts);
		pthread_mutex_unlock(&self->reaper.mutex);

		/* Highly possible the main thread woke us up to exit */
		if (!self->reaper.running) {
			return NULL;
		}

		/* Expire remaining entries */
		game_mode_context_auto_expire(self);

		ts.tv_sec = time(NULL) + reaper_interval;
	}

	return NULL;
}

GameModeContext *game_mode_context_instance()
{
	return &instance;
}

/**
 * Lookup the process environment for a specific variable or return NULL.
 * Requires an open directory FD from /proc/PID.
 */
static char *game_mode_lookup_proc_env(int proc_fd, const char *var)
{
	char *environ = NULL;

	int fd = openat(proc_fd, "environ", O_RDONLY | O_CLOEXEC);
	if (fd != -1) {
		FILE *stream = fdopen(fd, "r");
		if (stream) {
			/* Read every \0 terminated line from the environment */
			char *line = NULL;
			size_t len = 0;
			size_t pos = strlen(var) + 1;
			while (!environ && (getdelim(&line, &len, 0, stream) != -1)) {
				/* Find a match including the "=" suffix */
				if ((len > pos) && (strncmp(line, var, strlen(var)) == 0) && (line[pos - 1] == '='))
					environ = strndup(line + pos, len - pos);
			}
			free(line);
			fclose(stream);
		} else
			close(fd);
	}

	/* If found variable is empty, skip it */
	if (environ && !strlen(environ)) {
		free(environ);
		environ = NULL;
	}

	return environ;
}

/**
 * Lookup the home directory of the user in a safe way.
 */
static char *game_mode_lookup_user_home(void)
{
	/* Try loading env HOME first */
	const char *home = secure_getenv("HOME");
	if (!home) {
		/* If HOME is not defined (or out of context), fall back to passwd */
		struct passwd *pw = getpwuid(getuid());
		if (!pw)
			return NULL;
		home = pw->pw_dir;
	}

	/* Try to allocate into our heap */
	return home ? strdup(home) : NULL;
}

/**
 * Attempt to resolve the exe for wine-preloader.
 * This function is used if game_mode_context_find_exe() identified the
 * process as wine-preloader. Returns NULL when resolve fails.
 */
static char *game_mode_resolve_wine_preloader(pid_t pid)
{
	char buffer[PATH_MAX];
	char *proc_path = NULL, *wine_exe = NULL, *wineprefix = NULL;
	int proc_fd = -1;

	if (!(proc_path = safe_snprintf(buffer, "/proc/%d", pid)))
		goto fail;

	/* Open the directory, we are potentially reading multiple files from it */
	if (-1 == (proc_fd = open(proc_path, O_RDONLY | O_CLOEXEC)))
		goto fail_proc;

	/* Open the command line */
	int fd = openat(proc_fd, "cmdline", O_RDONLY | O_CLOEXEC);
	if (fd != -1) {
		FILE *stream = fdopen(fd, "r");
		if (stream) {
			char *argv = NULL;
			size_t args = 0;
			int argc = 0;
			while (!wine_exe && (argc++ < 2) && (getdelim(&argv, &args, 0, stream) != -1)) {
				/* If we see the wine loader here, we have to use the next argument */
				if (strtail(argv, "/wine") || strtail(argv, "/wine64"))
					continue;
				free(wine_exe); // just in case
				/* Check presence of the drive letter, we assume that below */
				wine_exe = args > 2 && argv[1] == ':' ? strndup(argv, args) : NULL;
			}
			free(argv);
			fclose(stream);
		} else
			close(fd);
	}

	/* Did we get wine exe from cmdline? */
	if (wine_exe)
		LOG_MSG("Detected wine exe for client %d [%s].\n", pid, wine_exe);
	else
		goto fail_cmdline;

	/* Open the process environment and find the WINEPREFIX */
	errno = 0;
	if (!(wineprefix = game_mode_lookup_proc_env(proc_fd, "WINEPREFIX"))) {
		/* Lookup user home instead only if there was no error */
		char *home = NULL;
		if (errno == 0)
			home = game_mode_lookup_user_home();

		/* Append "/.wine" if we found the user home */
		if (home)
			wineprefix = safe_snprintf(buffer, "%s/.wine", home);

		/* Cleanup and check result */
		free(home);
		if (!wineprefix)
			goto fail_env;
	}

	/* Wine prefix was detected, log this for diagnostics */
	LOG_MSG("Detected wine prefix for client %d: '%s'\n", pid, wineprefix);

	/* Convert Windows to Unix path separators */
	char *ix = wine_exe;
	while (ix != NULL)
		(ix = strchr(ix, '\\')) && (*ix++ = '/');

	/* Convert the drive letter to lcase because wine handles it this way in the prefix */
	wine_exe[0] = (char)tolower(wine_exe[0]);

	/* Convert relative wine exe path to full unix path */
	char *wine_path = safe_snprintf(buffer, "%s/dosdevices/%s", wineprefix, wine_exe);
	free(wine_exe);
	wine_exe = wine_path ? realpath(wine_path, NULL) : NULL;
	free(wine_path);

	/* Fine? Successo? Fortuna! */
	if (wine_exe)
		LOG_MSG("Successfully mapped wine client %d [%s].\n", pid, wine_exe);
	else
		goto fail;

error_cleanup:
	close(proc_fd);
	free(wineprefix);
	free(proc_path);
	return wine_exe;

fail:
	LOG_ERROR("Unable to find wine executable for client %d: %s\n", pid, strerror(errno));
	goto error_cleanup;

fail_cmdline:
	LOG_ERROR("Wine loader has no accepted cmdline for client %d yet, deferring.\n", pid);
	goto error_cleanup;

fail_env:
	LOG_ERROR("Failed to access process environment in '%s': %s\n", proc_path, strerror(errno));
	goto error_cleanup;

fail_proc:
	LOG_ERROR("Failed to access process data in '%s': %s\n", proc_path, strerror(errno));
	goto error_cleanup;
}

/**
 * Attempt to locate the exe for the process.
 * We might run into issues if the process is running under an odd umask.
 */
static char *game_mode_context_find_exe(pid_t pid)
{
	char buffer[PATH_MAX];
	char *proc_path = NULL, *wine_exe = NULL;

	if (!(proc_path = safe_snprintf(buffer, "/proc/%d/exe", pid)))
		goto fail;

	/* Allocate the realpath if possible */
	char *exe = realpath(proc_path, NULL);
	free(proc_path);
	if (!exe)
		goto fail;

	/* Detect if the process is a wine loader process */
	if (strtail(exe, "/wine-preloader") || strtail(exe, "/wine64-preloader")) {
		LOG_MSG("Detected wine preloader for client %d [%s].\n", pid, exe);
		goto wine_preloader;
	}
	if (strtail(exe, "/wine") || strtail(exe, "/wine64")) {
		LOG_MSG("Detected wine loader for client %d [%s].\n", pid, exe);
		goto wine_preloader;
	}

	return exe;

wine_preloader:

	wine_exe = game_mode_resolve_wine_preloader(pid);
	if (wine_exe) {
		free(exe);
		exe = wine_exe;
		return exe;
	}

	/* We have to ignore this because the wine process is in some sort
	 * of respawn mode
	 */
	free(exe);

fail:
	if (errno != 0) // otherwise a proper message was logged before
		LOG_ERROR("Unable to find executable for PID %d: %s\n", pid, strerror(errno));
	return NULL;
}
