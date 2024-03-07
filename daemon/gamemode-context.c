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

#include "common-external.h"
#include "common-governors.h"
#include "common-helpers.h"
#include "common-logging.h"
#include "common-power.h"

#include "gamemode.h"
#include "gamemode-config.h"

#include "build-config.h"

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/time.h>
#include <systemd/sd-daemon.h> /* TODO: Move usage to gamemode-dbus.c */
#include <unistd.h>

/**
 * The GameModeClient encapsulates the remote connection, providing a list
 * form to contain the pid and credentials.
 */
struct GameModeClient {
	_Atomic int refcount;        /**<Allow outside usage */
	pid_t pid;                   /**< Process ID */
	pid_t requester;             /**< Process ID that requested it */
	struct GameModeClient *next; /**<Next client in the list */
	char executable[PATH_MAX];   /**<Process executable */
	time_t timestamp;            /**<When was the client registered */
};

enum GameModeGovernor {
	GAME_MODE_GOVERNOR_DEFAULT,
	GAME_MODE_GOVERNOR_DESIRED,
	GAME_MODE_GOVERNOR_IGPU_DESIRED,
};

struct GameModeContext {
	pthread_rwlock_t rwlock; /**<Guard access to the client list */
	_Atomic int refcount;    /**<Allow cycling the game mode */
	GameModeClient *client;  /**<Pointer to first client */

	GameModeConfig *config; /**<Pointer to config object */

	char initial_cpu_mode[64]; /**<Only updates when we can */

	enum GameModeGovernor current_govenor;

	struct GameModeGPUInfo *stored_gpu; /**<Stored GPU info for the current GPU */
	struct GameModeGPUInfo *target_gpu; /**<Target GPU info for the current GPU */

	struct GameModeCPUInfo *cpu; /**<Stored CPU info for the current CPU */

	GameModeIdleInhibitor *idle_inhibitor;

	bool igpu_optimization_enabled;
	uint32_t last_cpu_energy_uj;
	uint32_t last_igpu_energy_uj;

	long initial_split_lock_mitigate;

	/* Reaper control */
	struct {
		pthread_t thread;
		bool running;
		pthread_mutex_t mutex;
		pthread_cond_t condition;
	} reaper;
};

static GameModeContext instance = { 0 };

/**
 * Protect against signals
 */
static volatile bool had_context_init = false;

static GameModeClient *game_mode_client_new(pid_t pid, char *exe, pid_t req);
static const GameModeClient *game_mode_context_has_client(GameModeContext *self, pid_t client);
static void *game_mode_context_reaper(void *userdata);
static void game_mode_context_enter(GameModeContext *self);
static void game_mode_context_leave(GameModeContext *self);
static char *game_mode_context_find_exe(pid_t pid);
static void game_mode_execute_scripts(char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX], int timeout);
static int game_mode_disable_splitlock(GameModeContext *self, bool disable);

static void start_reaper_thread(GameModeContext *self)
{
	pthread_mutex_init(&self->reaper.mutex, NULL);
	pthread_cond_init(&self->reaper.condition, NULL);

	self->reaper.running = true;
	if (pthread_create(&self->reaper.thread, NULL, game_mode_context_reaper, self) != 0) {
		FATAL_ERROR("Couldn't construct a new thread");
	}
}

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

	self->current_govenor = GAME_MODE_GOVERNOR_DEFAULT;

	/* Initialise the current GPU info */
	game_mode_initialise_gpu(self->config, &self->stored_gpu);
	game_mode_initialise_gpu(self->config, &self->target_gpu);

	/* Initialise the current CPU info */
	game_mode_initialise_cpu(self->config, &self->cpu);

	self->initial_split_lock_mitigate = -1;

	pthread_rwlock_init(&self->rwlock, NULL);

	/* Get the reaper thread going */
	start_reaper_thread(self);
}

static void end_reaper_thread(GameModeContext *self)
{
	self->reaper.running = false;

	/* We might be stuck waiting, so wake it up again */
	pthread_mutex_lock(&self->reaper.mutex);
	pthread_cond_signal(&self->reaper.condition);
	pthread_mutex_unlock(&self->reaper.mutex);

	/* Join the thread as soon as possible */
	pthread_join(self->reaper.thread, NULL);

	pthread_cond_destroy(&self->reaper.condition);
	pthread_mutex_destroy(&self->reaper.mutex);
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
	game_mode_client_unref(self->client);

	end_reaper_thread(self);

	/* Destroy the gpu object */
	game_mode_free_gpu(&self->stored_gpu);
	game_mode_free_gpu(&self->target_gpu);

	/* Destroy the cpu object */
	game_mode_free_cpu(&self->cpu);

	/* Destroy the config object */
	config_destroy(self->config);

	pthread_rwlock_destroy(&self->rwlock);
}

static int game_mode_disable_splitlock(GameModeContext *self, bool disable)
{
	if (!config_get_disable_splitlock(self->config))
		return 0;

	long value_num = self->initial_split_lock_mitigate;
	char value_str[40];

	if (disable) {
		FILE *f = fopen("/proc/sys/kernel/split_lock_mitigate", "r");
		if (f == NULL) {
			if (errno == ENOENT)
				return 0;

			LOG_ERROR("Couldn't open /proc/sys/kernel/split_lock_mitigate : %s\n", strerror(errno));
			return 1;
		}

		if (fgets(value_str, sizeof value_str, f) == NULL) {
			LOG_ERROR("Couldn't read from /proc/sys/kernel/split_lock_mitigate : %s\n",
			          strerror(errno));
			fclose(f);
			return 1;
		}

		self->initial_split_lock_mitigate = strtol(value_str, NULL, 10);
		fclose(f);

		value_num = 0;
		if (self->initial_split_lock_mitigate == value_num)
			return 0;
	}

	if (value_num == -1)
		return 0;

	sprintf(value_str, "%ld", value_num);

	const char *const exec_args[] = {
		"pkexec", LIBEXECDIR "/procsysctl", "split_lock_mitigate", value_str, NULL,
	};

	LOG_MSG("Requesting update of split_lock_mitigate to %s\n", value_str);
	int ret = run_external_process(exec_args, NULL, -1);
	if (ret != 0) {
		LOG_ERROR("Failed to update split_lock_mitigate\n");
		return ret;
	}

	return 0;
}

static int game_mode_set_governor(GameModeContext *self, enum GameModeGovernor gov)
{
	if (self->current_govenor == gov) {
		return 0;
	}

	if (self->current_govenor == GAME_MODE_GOVERNOR_DEFAULT) {
		/* Read the initial governor state so we can revert it correctly */
		const char *initial_state = get_gov_state();
		if (initial_state == NULL) {
			return 0;
		}

		/* store the initial cpu governor mode */
		strncpy(self->initial_cpu_mode, initial_state, sizeof(self->initial_cpu_mode) - 1);
		self->initial_cpu_mode[sizeof(self->initial_cpu_mode) - 1] = '\0';
		LOG_MSG("governor was initially set to [%s]\n", initial_state);
	}

	char *gov_str = NULL;
	char gov_config_str[CONFIG_VALUE_MAX] = { 0 };
	switch (gov) {
	case GAME_MODE_GOVERNOR_DEFAULT:
		config_get_default_governor(self->config, gov_config_str);
		gov_str = gov_config_str[0] != '\0' ? gov_config_str : self->initial_cpu_mode;
		break;

	case GAME_MODE_GOVERNOR_DESIRED:
		config_get_desired_governor(self->config, gov_config_str);
		gov_str = gov_config_str[0] != '\0' ? gov_config_str : "performance";
		break;

	case GAME_MODE_GOVERNOR_IGPU_DESIRED:
		config_get_igpu_desired_governor(self->config, gov_config_str);
		gov_str = gov_config_str[0] != '\0' ? gov_config_str : "powersave";
		break;

	default:
		assert(!"Invalid governor requested");
	}

	const char *const exec_args[] = {
		"pkexec", LIBEXECDIR "/cpugovctl", "set", gov_str, NULL,
	};

	LOG_MSG("Requesting update of governor policy to %s\n", gov_str);
	int ret = run_external_process(exec_args, NULL, -1);
	if (ret != 0) {
		LOG_ERROR("Failed to update cpu governor policy\n");
		return ret;
	}

	/* Update the current govenor only if we succeed at setting govenors. */
	self->current_govenor = gov;

	return 0;
}

static void game_mode_enable_igpu_optimization(GameModeContext *self)
{
	float threshold = config_get_igpu_power_threshold(self->config);

	/* There's no way the GPU is using 10000x the power.  This lets us
	 * short-circuit if the config file specifies an invalid threshold
	 * and we want to disable the iGPU heuristic.
	 */
	if (threshold < 10000 && get_cpu_energy_uj(&self->last_cpu_energy_uj) &&
	    get_igpu_energy_uj(&self->last_igpu_energy_uj)) {
		LOG_MSG(
		    "Successfully queried power data for the CPU and iGPU. "
		    "Enabling the integrated GPU optimization");
		self->igpu_optimization_enabled = true;
	}
}

static void game_mode_disable_igpu_optimization(GameModeContext *self)
{
	self->igpu_optimization_enabled = false;
}

static void game_mode_check_igpu_energy(GameModeContext *self)
{
	pthread_rwlock_wrlock(&self->rwlock);

	/* We only care if we're not in the default governor */
	if (self->current_govenor == GAME_MODE_GOVERNOR_DEFAULT)
		goto unlock;

	if (!self->igpu_optimization_enabled)
		goto unlock;

	uint32_t cpu_energy_uj, igpu_energy_uj;
	if (!get_cpu_energy_uj(&cpu_energy_uj) || !get_igpu_energy_uj(&igpu_energy_uj)) {
		/* We've already succeeded at getting power information once so
		 * failing here is possible but very unexpected. */
		self->igpu_optimization_enabled = false;
		LOG_ERROR("Failed to get CPU and iGPU power data\n");
		goto unlock;
	}

	/* The values we query from RAPL are in units of microjoules of energy
	 * used since boot or since the last time the counter rolled over.  You
	 * can get average power over some time window T by sampling before and
	 * after and doing the following calculation
	 *
	 *     power_uw = (energy_uj_after - energy_uj_before) / seconds
	 *
	 * To get the power in Watts (rather than microwatts), you can simply
	 * divide by 1000000.
	 *
	 * Because we're only concerned with the ratio between the GPU and CPU
	 * power, we never bother dividing by 1000000 the length of time of the
	 * sampling window because that would just algebraically cancel out.
	 * Instead, we divide the GPU energy used in the window (difference of
	 * before and after) by the CPU energy used. It nicely provides the
	 * ratio of the averages and there are no instantaneous sampling
	 * problems.
	 *
	 * Overflow is possible here.  However, that would simply mean that
	 * the HW counter has overflowed and us wrapping around is probably
	 * the right thing to do.  Wrapping at 32 bits is exactly what the
	 * Linux kernel's turbostat utility does so it's probably right.
	 */
	uint32_t cpu_energy_delta_uj = cpu_energy_uj - self->last_cpu_energy_uj;
	uint32_t igpu_energy_delta_uj = igpu_energy_uj - self->last_igpu_energy_uj;
	self->last_cpu_energy_uj = cpu_energy_uj;
	self->last_igpu_energy_uj = igpu_energy_uj;

	if (cpu_energy_delta_uj == 0) {
		LOG_ERROR("CPU reported no energy used\n");
		goto unlock;
	}

	float threshold = config_get_igpu_power_threshold(self->config);
	double ratio = (double)igpu_energy_delta_uj / (double)cpu_energy_delta_uj;
	if (ratio > threshold) {
		game_mode_set_governor(self, GAME_MODE_GOVERNOR_IGPU_DESIRED);
	} else {
		game_mode_set_governor(self, GAME_MODE_GOVERNOR_DESIRED);
	}

unlock:
	pthread_rwlock_unlock(&self->rwlock);
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

	if (game_mode_set_governor(self, GAME_MODE_GOVERNOR_DESIRED) == 0) {
		/* We just switched to a non-default governor.  Enable the iGPU
		 * optimization.
		 */
		game_mode_enable_igpu_optimization(self);
	}

	/* Inhibit the screensaver */
	if (config_get_inhibit_screensaver(self->config)) {
		game_mode_destroy_idle_inhibitor(self->idle_inhibitor);
		self->idle_inhibitor = game_mode_create_idle_inhibitor();
	}

	game_mode_disable_splitlock(self, true);

	/* Apply GPU optimisations by first getting the current values, and then setting the target */
	game_mode_get_gpu(self->stored_gpu);
	game_mode_apply_gpu(self->target_gpu);

	game_mode_park_cpu(self->cpu);

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

	game_mode_unpark_cpu(self->cpu);

	/* UnInhibit the screensaver */
	if (config_get_inhibit_screensaver(self->config)) {
		game_mode_destroy_idle_inhibitor(self->idle_inhibitor);
		self->idle_inhibitor = NULL;
	}

	game_mode_disable_splitlock(self, false);

	game_mode_set_governor(self, GAME_MODE_GOVERNOR_DEFAULT);

	game_mode_disable_igpu_optimization(self);

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

int game_mode_context_num_clients(GameModeContext *self)
{
	return atomic_load(&self->refcount);
}

pid_t *game_mode_context_list_clients(GameModeContext *self, unsigned int *count)
{
	pid_t *res = NULL;
	unsigned int i = 0;
	unsigned int n;

	pthread_rwlock_rdlock(&self->rwlock);
	n = (unsigned int)atomic_load(&self->refcount);

	if (n > 0)
		res = (pid_t *)malloc(n * sizeof(pid_t));

	for (GameModeClient *cl = self->client; cl; cl = cl->next) {
		assert(n > i);

		res[i] = cl->pid;
		i++;
	}

	*count = i;

	pthread_rwlock_unlock(&self->rwlock);
	return res;
}

GameModeClient *game_mode_context_lookup_client(GameModeContext *self, pid_t client)
{
	GameModeClient *found = NULL;

	pthread_rwlock_rdlock(&self->rwlock);

	/* Walk all clients and find a matching pid */
	for (GameModeClient *cl = self->client; cl; cl = cl->next) {
		if (cl->pid == client) {
			found = cl;
			break;
		}
	}

	if (found) {
		game_mode_client_ref(found);
	}

	pthread_rwlock_unlock(&self->rwlock);

	return found;
}

static int game_mode_apply_client_optimisations(GameModeContext *self, pid_t client)
{
	/* Store current renice and apply */
	game_mode_apply_renice(self, client, 0 /* expect zero value to start with */);

	/* Store current ioprio value and apply  */
	game_mode_apply_ioprio(self, client, IOPRIO_DEFAULT);

	/* Apply scheduler policies */
	game_mode_apply_scheduling(self, client);

	/* Apply core pinning */
	game_mode_apply_core_pinning(self->cpu, client, false);

	return 0;
}

int game_mode_context_register(GameModeContext *self, pid_t client, pid_t requester)
{
	errno = 0;

	/* Construct a new client if we can */
	char *executable = NULL;
	int err = -1;

	/* Check our requester config first */
	if (requester != client) {
		/* Lookup the executable first */
		executable = game_mode_context_find_exe(requester);
		if (!executable) {
			goto error_cleanup;
		}

		/* Check our blacklist and whitelist */
		if (!config_get_supervisor_whitelisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (not in whitelist)\n", executable);
			err = -2;
			goto error_cleanup;
		} else if (config_get_supervisor_blacklisted(self->config, executable)) {
			LOG_MSG("Supervisor [%s] was rejected (in blacklist)\n", executable);
			err = -2;
			goto error_cleanup;
		}

		/* We're done with the requestor */
		free(executable);
		executable = NULL;
	} else if (config_get_require_supervisor(self->config)) {
		LOG_ERROR("Direct request made but require_supervisor was set, rejecting request!\n");
		err = -2;
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
	GameModeClient *cl = game_mode_client_new(client, executable, requester);
	if (!cl)
		goto error_cleanup;
	free(executable); /* we're now done with memory */

	/* Begin a write lock now to insert our new client at list start */
	pthread_rwlock_wrlock(&self->rwlock);

	LOG_MSG("Adding game: %d [%s]\n", client, cl->executable);

	/* Update the list */
	cl->next = self->client;
	self->client = cl;

	/* First add, init */
	if (atomic_fetch_add_explicit(&self->refcount, 1, memory_order_seq_cst) == 0) {
		game_mode_context_enter(self);
	}

	game_mode_apply_client_optimisations(self, client);

	/* Unlock now we're done applying optimisations */
	pthread_rwlock_unlock(&self->rwlock);

	game_mode_client_registered(client);

	return 0;

error_cleanup:
	if (errno != 0)
		LOG_ERROR("Failed to register client [%d]: %s\n", client, strerror(errno));
	free(executable);
	return err;
}

static int game_mode_remove_client_optimisations(GameModeContext *self, pid_t client)
{
	/* Restore the ioprio value for the process, expecting it to be the config value  */
	game_mode_apply_ioprio(self, client, (int)config_get_ioprio_value(self->config));

	/* Restore the renice value for the process, expecting it to be our config value */
	game_mode_apply_renice(self, client, (int)config_get_renice_value(self->config));

	/* Restore the process affinity to all online cores */
	game_mode_undo_core_pinning(self->cpu, client);
	return 0;
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
		game_mode_client_unref(cl);
		break;
	}

	if (!found) {
		LOG_HINTED(
		    ERROR,
		    "Removal requested for unknown process [%d].\n",
		    "    -- The parent process probably forked and tries to unregister from the wrong\n"
		    "    -- process now. We cannot work around this. This message will likely be paired\n"
		    "    -- with a nearby 'Removing expired game' which means we cleaned up properly\n"
		    "    -- (we will log this event). This hint will be displayed only once.\n",
		    client);
		pthread_rwlock_unlock(&self->rwlock);
		return -1;
	}

	/* When we hit bottom then end the game mode */
	if (atomic_fetch_sub_explicit(&self->refcount, 1, memory_order_seq_cst) == 1) {
		game_mode_context_leave(self);
	}

	game_mode_remove_client_optimisations(self, client);

	/* Unlock now we're done applying optimisations */
	pthread_rwlock_unlock(&self->rwlock);

	game_mode_client_unregistered(client);

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
static GameModeClient *game_mode_client_new(pid_t pid, char *executable, pid_t requester)
{
	/* This bit seems to be formatted differently by different clang-format versions */
	/* clang-format off */
	GameModeClient c = {
		.next = NULL,
		.pid = pid,
		.requester = requester,
		.timestamp = 0,
	};
	/* clang-format on */
	GameModeClient *ret = NULL;
	struct timeval now = {
		0,
	};
	int r;

	r = gettimeofday(&now, NULL);
	if (r == 0)
		c.timestamp = now.tv_sec;

	ret = calloc(1, sizeof(struct GameModeClient));
	if (!ret) {
		return NULL;
	}
	*ret = c;
	ret->refcount = ATOMIC_VAR_INIT(1);
	strncpy(ret->executable, executable, PATH_MAX - 1);

	return ret;
}

/**
 * Unref a client and the next element in the list, if non-null.
 */
void game_mode_client_unref(GameModeClient *client)
{
	if (!client) {
		return;
	}
	if (atomic_fetch_sub_explicit(&client->refcount, 1, memory_order_seq_cst) > 1) {
		return; /* object is still alive */
	}
	if (client->next) {
		game_mode_client_unref(client->next);
	}
	free(client);
}

void game_mode_client_ref(GameModeClient *client)
{
	if (!client) {
		return;
	}
	atomic_fetch_add_explicit(&client->refcount, 1, memory_order_seq_cst);
}

/**
 * The process identifier of the client.
 */
pid_t game_mode_client_get_pid(GameModeClient *client)
{
	assert(client != NULL);
	return client->pid;
}

/**
 * The path to the executable of client.
 */
const char *game_mode_client_get_executable(GameModeClient *client)
{
	assert(client != NULL);
	return client->executable;
}

/**
 * The process identifier of the requester.
 */
pid_t game_mode_client_get_requester(GameModeClient *client)
{
	assert(client != NULL);
	return client->requester;
}

/**
 * The time that game mode was requested for the client.
 */
uint64_t game_mode_client_get_timestamp(GameModeClient *client)
{
	assert(client != NULL);
	return (uint64_t)client->timestamp;
}

static void game_mode_reapply_core_pinning_internal(GameModeContext *self)
{
	pthread_rwlock_wrlock(&self->rwlock);
	if (game_mode_context_num_clients(self)) {
		for (GameModeClient *cl = self->client; cl; cl = cl->next)
			game_mode_apply_core_pinning(self->cpu, cl->pid, true);
	}
	pthread_rwlock_unlock(&self->rwlock);
}

/* Internal refresh config function (assumes no contention with reaper thread) */
static void game_mode_reload_config_internal(GameModeContext *self)
{
	LOG_MSG("Reloading config...\n");

	/* Make sure we have a readwrite lock on ourselves */
	pthread_rwlock_wrlock(&self->rwlock);

	/* Remove current optimisations when we're already active */
	if (game_mode_context_num_clients(self)) {
		for (GameModeClient *cl = self->client; cl; cl = cl->next)
			game_mode_remove_client_optimisations(self, cl->pid);

		game_mode_context_leave(self);
	}

	/* Reload the config */
	config_reload(self->config);
	game_mode_reconfig_cpu(self->config, &self->cpu);

	/* Re-apply all current optimisations */
	if (game_mode_context_num_clients(self)) {
		/* Start the global context back up */
		game_mode_context_enter(self);

		for (GameModeClient *cl = self->client; cl; cl = cl->next)
			game_mode_apply_client_optimisations(self, cl->pid);
	}

	pthread_rwlock_unlock(&self->rwlock);

	LOG_MSG("Config reload complete\n");
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

		/* Check on the CPU/iGPU energy balance */
		game_mode_check_igpu_energy(self);

		/* Expire remaining entries */
		game_mode_context_auto_expire(self);

		/* Re apply the thread affinity mask (aka core pinning) */
		game_mode_reapply_core_pinning_internal(self);

		/* Check if we should be reloading the config, and do so if needed */
		if (config_needs_reload(self->config)) {
			LOG_MSG("Detected config file changes\n");
			game_mode_reload_config_internal(self);
		}

		ts.tv_sec = time(NULL) + reaper_interval;
	}

	return NULL;
}

GameModeContext *game_mode_context_instance(void)
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
	autoclose_fd int pidfd = -1;
	ssize_t r;

	if (!(proc_path = buffered_snprintf(buffer, "/proc/%d", pid)))
		goto fail;

	/* Translate /proc/<pid>/exe to the application binary */
	pidfd = openat(AT_FDCWD, proc_path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
	if (pidfd == -1)
		goto fail;

	r = readlinkat(pidfd, "exe", buffer, sizeof(buffer));

	if (r == sizeof(buffer)) {
		errno = ENAMETOOLONG;
		r = -1;
	}

	if (r == -1)
		goto fail;

	buffer[r] = '\0';

	char *exe = strdup(buffer);

	/* Resolve for wine if appropriate */
	if ((wine_exe = game_mode_resolve_wine_preloader(exe, pid))) {
		free(exe);
		exe = wine_exe;
	}

	return exe;

fail:
	if (errno != 0) // otherwise a proper message was logged before
		LOG_ERROR("Unable to find executable for PID %d: %s\n", pid, strerror(errno));
	return NULL;
}

/* Executes a set of scripts */
static void game_mode_execute_scripts(char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX], int timeout)
{
	unsigned int i = 0;
	while (i < CONFIG_LIST_MAX && *scripts[i] != '\0') {
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

/*
 * Reload the current configuration
 *
 * Reloading the configuration completely live would be problematic for various optimisation values,
 * to ensure we have a fully clean state, we tear down the whole gamemode state and regrow it with a
 * new config, remembering the registered games
 */
int game_mode_reload_config(GameModeContext *self)
{
	/* Stop the reaper thread first */
	end_reaper_thread(self);

	game_mode_reload_config_internal(self);

	/* Restart the reaper thread back up again */
	start_reaper_thread(self);

	return 0;
}
