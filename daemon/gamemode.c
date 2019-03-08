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
#include "config.h"
#include "daemon_config.h"
#include "dbus_messaging.h"
#include "external-helper.h"
#include "governors-query.h"
#include "helpers.h"
#include "logging.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <systemd/sd-daemon.h>

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

	struct GameModeGPUInfo *stored_gpu; /**<Stored GPU info for the current GPU */
	struct GameModeGPUInfo *target_gpu; /**<Target GPU info for the current GPU */

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
static const GameModeClient *game_mode_context_has_client(GameModeContext *self, pid_t client);
static int game_mode_context_num_clients(GameModeContext *self);
static void *game_mode_context_reaper(void *userdata);
static void game_mode_context_enter(GameModeContext *self);
static void game_mode_context_leave(GameModeContext *self);
static char *game_mode_context_find_exe(pid_t pid);
static void game_mode_execute_scripts(char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX], int timeout);

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

	/* Initialise the current GPU info */
	game_mode_initialise_gpu(self->config, &self->stored_gpu);
	game_mode_initialise_gpu(self->config, &self->target_gpu);

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

	/* Destroy the gpu object */
	game_mode_free_gpu(&self->stored_gpu);
	game_mode_free_gpu(&self->target_gpu);

	/* Destroy the config object */
	config_destroy(self->config);

	pthread_rwlock_destroy(&self->rwlock);
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

		const char *const exec_args[] = {
			"/usr/bin/pkexec", LIBEXECDIR "/cpugovctl", "set", desiredGov, NULL,
		};

		LOG_MSG("Requesting update of governor policy to %s\n", desiredGov);
		if (run_external_process(exec_args, NULL, -1) != 0) {
			LOG_ERROR("Failed to update cpu governor policy\n");
			/* if the set fails, clear the initial mode so we don't try and reset it back and fail
			 * again, presumably */
			memset(self->initial_cpu_mode, 0, sizeof(self->initial_cpu_mode));
		}
	}

	/* Inhibit the screensaver */
	if (config_get_inhibit_screensaver(self->config))
		game_mode_inhibit_screensaver(true);

	/* Apply GPU optimisations by first getting the current values, and then setting the target */
	game_mode_get_gpu(self->stored_gpu);
	game_mode_apply_gpu(self->target_gpu);

	/* Run custom scripts last - ensures the above are applied first and these scripts can react to
	 * them if needed */
	char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	memset(scripts, 0, sizeof(scripts));
	config_get_gamemode_start_scripts(self->config, scripts);
	long timeout = config_get_script_timeout(self->config);
	game_mode_execute_scripts(scripts, (int)timeout);
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

	/* Remove GPU optimisations */
	game_mode_apply_gpu(self->stored_gpu);

	/* UnInhibit the screensaver */
	if (config_get_inhibit_screensaver(self->config))
		game_mode_inhibit_screensaver(false);

	/* Reset the governer state back to initial */
	if (self->initial_cpu_mode[0] != '\0') {
		/* Choose the governor to reset to, using the config to override */
		char defaultgov[CONFIG_VALUE_MAX] = { 0 };
		config_get_default_governor(self->config, defaultgov);
		const char *gov_mode = defaultgov[0] != '\0' ? defaultgov : self->initial_cpu_mode;

		const char *const exec_args[] = {
			"/usr/bin/pkexec", LIBEXECDIR "/cpugovctl", "set", gov_mode, NULL,
		};

		LOG_MSG("Requesting update of governor policy to %s\n", gov_mode);
		if (run_external_process(exec_args, NULL, -1) != 0) {
			LOG_ERROR("Failed to update cpu governor policy\n");
		}

		memset(self->initial_cpu_mode, 0, sizeof(self->initial_cpu_mode));
	}

	char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	memset(scripts, 0, sizeof(scripts));
	config_get_gamemode_end_scripts(self->config, scripts);
	long timeout = config_get_script_timeout(self->config);
	game_mode_execute_scripts(scripts, (int)timeout);
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
				game_mode_context_unregister(self, client->pid, client->pid);
				removing = true;
				break;
			}
		}

		if (!removing) {
			pthread_rwlock_unlock(&self->rwlock);
			break;
		}

		if (game_mode_context_num_clients(self) == 0)
			LOG_MSG("Properly cleaned up all expired games.\n");
	}
}

/**
 * Determine if the client is already known to the context
 */
static const GameModeClient *game_mode_context_has_client(GameModeContext *self, pid_t client)
{
	const GameModeClient *found = NULL;
	pthread_rwlock_rdlock(&self->rwlock);

	/* Walk all clients and find a matching pid */
	for (GameModeClient *cl = self->client; cl; cl = cl->next) {
		if (cl->pid == client) {
			found = cl;
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

int game_mode_context_register(GameModeContext *self, pid_t client, pid_t requester)
{
	errno = 0;

	/* Construct a new client if we can */
	GameModeClient *cl = NULL;
	char *executable = NULL;

	/* Check our requester config first */
	if (requester != client) {
		/* Lookup the executable first */
		executable = game_mode_context_find_exe(requester);
		if (!executable) {
			return -1;
		}

		/* Check our blacklist and whitelist */
		if (!config_get_supervisor_whitelisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (not in whitelist)\n", executable);
			free(executable);
			return -2;
		} else if (config_get_supervisor_blacklisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (in blacklist)\n", executable);
			free(executable);
			return -2;
		}
	} else if (config_get_require_supervisor(self->config)) {
		LOG_ERROR("Direct request made but require_supervisor was set, rejecting request!\n");
		return -2;
	}

	/* Cap the total number of active clients */
	if (game_mode_context_num_clients(self) + 1 > MAX_GAMES) {
		LOG_ERROR("Max games (%d) reached, not registering %d\n", MAX_GAMES, client);
		goto error_cleanup;
	}

	/* Check the PID first to spare a potentially expensive lookup for the exe */
	pthread_rwlock_rdlock(&self->rwlock); // ensure our pointer is sane
	const GameModeClient *existing = game_mode_context_has_client(self, client);
	if (existing) {
		LOG_HINTED(ERROR,
		           "Addition requested for already known client %d [%s].\n",
		           "    -- This may happen due to using exec or shell wrappers. You may want to\n"
		           "    -- blacklist this client so GameMode can see its final name here.\n",
		           existing->pid,
		           existing->executable);
		pthread_rwlock_unlock(&self->rwlock);
		goto error_cleanup;
	}
	pthread_rwlock_unlock(&self->rwlock);

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
	game_mode_apply_renice(self, client);
	game_mode_apply_scheduling(self, client);

	/* Apply io priorities */
	game_mode_apply_ioprio(self, client);

	return 0;

error_cleanup:
	if (errno != 0)
		LOG_ERROR("Failed to register client [%d]: %s\n", client, strerror(errno));
	free(executable);
	game_mode_client_free(cl);
	return -1;
}

int game_mode_context_unregister(GameModeContext *self, pid_t client, pid_t requester)
{
	GameModeClient *cl = NULL;
	GameModeClient *prev = NULL;
	bool found = false;

	/* Check our requester config first */
	if (requester != client) {
		/* Lookup the executable first */
		char *executable = game_mode_context_find_exe(requester);
		if (!executable) {
			return -1;
		}

		/* Check our blacklist and whitelist */
		if (!config_get_supervisor_whitelisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (not in whitelist)\n", executable);
			free(executable);
			return -2;
		} else if (config_get_supervisor_blacklisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (in blacklist)\n", executable);
			free(executable);
			return -2;
		}

		free(executable);
	} else if (config_get_require_supervisor(self->config)) {
		LOG_ERROR("Direct request made but require_supervisor was set, rejecting request!\n");
		return -2;
	}

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
		LOG_HINTED(
		    ERROR,
		    "Removal requested for unknown process [%d].\n",
		    "    -- The parent process probably forked and tries to unregister from the wrong\n"
		    "    -- process now. We cannot work around this. This message will likely be paired\n"
		    "    -- with a nearby 'Removing expired game' which means we cleaned up properly\n"
		    "    -- (we will log this event). This hint will be displayed only once.\n",
		    client);
		return -1;
	}

	/* When we hit bottom then end the game mode */
	if (atomic_fetch_sub_explicit(&self->refcount, 1, memory_order_seq_cst) == 1) {
		game_mode_context_leave(self);
	}

	return 0;
}

int game_mode_context_query_status(GameModeContext *self, pid_t client, pid_t requester)
{
	GameModeClient *cl = NULL;
	int ret = 0;

	/* First check the requester settings if appropriate */
	if (client != requester) {
		char *executable = game_mode_context_find_exe(requester);
		if (!executable) {
			return -1;
		}

		/* Check our blacklist and whitelist */
		if (!config_get_supervisor_whitelisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (not in whitelist)\n", executable);
			free(executable);
			return -2;
		} else if (config_get_supervisor_blacklisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (in blacklist)\n", executable);
			free(executable);
			return -2;
		}

		free(executable);
	}

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

	long reaper_interval = config_get_reaper_frequency(self->config);

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

GameModeConfig *game_mode_config_from_context(const GameModeContext *context)
{
	return context ? context->config : NULL;
}

/**
 * Attempt to locate the exe for the process.
 * We might run into issues if the process is running under an odd umask.
 */
static char *game_mode_context_find_exe(pid_t pid)
{
	char buffer[PATH_MAX];
	char *proc_path = NULL, *wine_exe = NULL;

	if (!(proc_path = buffered_snprintf(buffer, "/proc/%d/exe", pid)))
		goto fail;

	/* Allocate the realpath if possible */
	char *exe = realpath(proc_path, NULL);
	if (!exe)
		goto fail;

	/* Detect if the process is a wine loader process */
	if (game_mode_detect_wine_preloader(exe)) {
		LOG_MSG("Detected wine preloader for client %d [%s].\n", pid, exe);
		goto wine_preloader;
	}
	if (game_mode_detect_wine_loader(exe)) {
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

/* Executes a set of scripts */
static void game_mode_execute_scripts(char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX], int timeout)
{
	unsigned int i = 0;
	while (*scripts[i] != '\0' && i < CONFIG_LIST_MAX) {
		LOG_MSG("Executing script [%s]\n", scripts[i]);
		int err;
		const char *args[] = { "/bin/sh", "-c", scripts[i], NULL };
		if ((err = run_external_process(args, NULL, timeout)) != 0) {
			/* Log the failure, but this is not fatal */
			LOG_ERROR("Script [%s] failed with error %d\n", scripts[i], err);
		}
		i++;
	}
}
