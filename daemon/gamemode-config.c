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

#include "gamemode-config.h"

#include "common-helpers.h"
#include "common-logging.h"

#include "build-config.h"

/* Ben Hoyt's inih library */
#include <ini.h>

#include <dirent.h>
#include <libgen.h>
#include <math.h>
#include <pthread.h>
#include <pwd.h>
#include <sys/inotify.h>
#include <sys/stat.h>

/* Name and possible location of the config file */
#define CONFIG_NAME "gamemode.ini"

/* Default value for the reaper frequency */
#define DEFAULT_REAPER_FREQ 5

#define DEFAULT_IGPU_POWER_THRESHOLD 0.3f

/* Helper macro for defining the config variable getter */
#define DEFINE_CONFIG_GET(name)                                                                    \
	long config_get_##name(GameModeConfig *self)                                                   \
	{                                                                                              \
		long value = 0;                                                                            \
		memcpy_locked_config(self, &value, &self->values.name, sizeof(long));                      \
		return value;                                                                              \
	}

/* The number of current locations for config files */
#define CONFIG_NUM_LOCATIONS 4

/**
 * The config holds various details as needed
 * and a rwlock to allow config_reload to be called
 */
struct GameModeConfig {
	pthread_rwlock_t rwlock;
	int inotfd;
	int inotwd[CONFIG_NUM_LOCATIONS];

	struct {
		char whitelist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
		char blacklist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];

		long script_timeout;
		char startscripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
		char endscripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];

		char defaultgov[CONFIG_VALUE_MAX];
		char desiredgov[CONFIG_VALUE_MAX];

		char igpu_desiredgov[CONFIG_VALUE_MAX];
		float igpu_power_threshold;

		char softrealtime[CONFIG_VALUE_MAX];
		long renice;

		char ioprio[CONFIG_VALUE_MAX];

		long inhibit_screensaver;

		long disable_splitlock;

		long reaper_frequency;

		char apply_gpu_optimisations[CONFIG_VALUE_MAX];
		long gpu_device;
		long nv_core_clock_mhz_offset;
		long nv_mem_clock_mhz_offset;
		long nv_powermizer_mode;
		char amd_performance_level[CONFIG_VALUE_MAX];

		char cpu_park_cores[CONFIG_VALUE_MAX];
		char cpu_pin_cores[CONFIG_VALUE_MAX];

		long require_supervisor;
		char supervisor_whitelist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
		char supervisor_blacklist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	} values;
};

/*
 * Add values to a char list
 */
static bool append_value_to_list(const char *list_name, const char *value,
                                 char list[CONFIG_LIST_MAX][CONFIG_VALUE_MAX])
{
	unsigned int i = 0;
	while (*list[i] && ++i < CONFIG_LIST_MAX)
		;

	if (i < CONFIG_LIST_MAX) {
		strncpy(list[i], value, CONFIG_VALUE_MAX);

		if (list[i][CONFIG_VALUE_MAX - 1] != '\0') {
			LOG_ERROR("Config: Could not add [%s] to [%s], exceeds length limit of %d\n",
			          value,
			          list_name,
			          CONFIG_VALUE_MAX);

			memset(list[i], 0, sizeof(list[i]));
			return false;
		}
	} else {
		LOG_ERROR("Config: Could not add [%s] to [%s], exceeds number of %d\n",
		          value,
		          list_name,
		          CONFIG_LIST_MAX);
		return false;
	}

	return true;
}

/*
 * Get a long value from a string
 */
static bool get_long_value(const char *value_name, const char *value, long *output)
{
	char *end = NULL;
	long config_value = strtol(value, &end, 10);

	if (errno == ERANGE) {
		LOG_ERROR("Config: %s overflowed, given [%s]\n", value_name, value);
		return false;
	} else if (!(*value != '\0' && end && *end == '\0')) {
		LOG_ERROR("Config: %s was invalid, given [%s]\n", value_name, value);
		return false;
	} else {
		*output = config_value;
	}

	return true;
}
/*
 * Get a long value from a hex string
 */
__attribute__((unused)) static bool get_long_value_hex(const char *value_name, const char *value,
                                                       long *output)
{
	char *end = NULL;
	long config_value = strtol(value, &end, 16);

	if (errno == ERANGE) {
		LOG_ERROR("Config: %s overflowed, given [%s]\n", value_name, value);
		return false;
	} else if (!(*value != '\0' && end && *end == '\0')) {
		LOG_ERROR("Config: %s was invalid, given [%s]\n", value_name, value);
		return false;
	} else {
		*output = config_value;
	}

	return true;
}

/*
 * Get a long value from a string
 */
static bool get_float_value(const char *value_name, const char *value, float *output)
{
	char *end = NULL;
	float config_value = strtof(value, &end);

	if (errno == ERANGE) {
		LOG_ERROR("Config: %s overflowed, given [%s]\n", value_name, value);
		return false;
	} else if (!(*value != '\0' && end && *end == '\0')) {
		LOG_ERROR("Config: %s was invalid, given [%s]\n", value_name, value);
		return false;
	} else {
		*output = config_value;
	}

	return true;
}

/**
 * Simple strstr scheck
 * Could be expanded for wildcard or regex
 */
static bool config_string_list_contains(const char *needle,
                                        char haystack[CONFIG_LIST_MAX][CONFIG_VALUE_MAX])
{
	for (unsigned int i = 0; i < CONFIG_LIST_MAX && haystack[i][0]; i++) {
		if (strstr(needle, haystack[i])) {
			return true;
		}
	}
	return false;
}

/*
 * Get a string value
 */
static bool get_string_value(const char *value, char output[CONFIG_VALUE_MAX])
{
	strncpy(output, value, CONFIG_VALUE_MAX - 1);
	output[CONFIG_VALUE_MAX - 1] = '\0';
	return true;
}

/* Controls whether to read the protected config variables */
static bool load_protected = false;

/*
 * Handler for the inih callback
 */
static int inih_handler(void *user, const char *section, const char *name, const char *value)
{
	GameModeConfig *self = (GameModeConfig *)user;
	bool valid = false;

	if (strcmp(section, "filter") == 0) {
		/* Filter subsection */
		if (strcmp(name, "whitelist") == 0) {
			valid = append_value_to_list(name, value, self->values.whitelist);
		} else if (strcmp(name, "blacklist") == 0) {
			valid = append_value_to_list(name, value, self->values.blacklist);
		}
	} else if (strcmp(section, "general") == 0) {
		/* General subsection */
		if (strcmp(name, "reaper_freq") == 0) {
			valid = get_long_value(name, value, &self->values.reaper_frequency);
		} else if (strcmp(name, "defaultgov") == 0) {
			valid = get_string_value(value, self->values.defaultgov);
		} else if (strcmp(name, "desiredgov") == 0) {
			valid = get_string_value(value, self->values.desiredgov);
		} else if (strcmp(name, "igpu_desiredgov") == 0) {
			valid = get_string_value(value, self->values.igpu_desiredgov);
		} else if (strcmp(name, "igpu_power_threshold") == 0) {
			valid = get_float_value(name, value, &self->values.igpu_power_threshold);
		} else if (strcmp(name, "softrealtime") == 0) {
			valid = get_string_value(value, self->values.softrealtime);
		} else if (strcmp(name, "renice") == 0) {
			valid = get_long_value(name, value, &self->values.renice);
		} else if (strcmp(name, "ioprio") == 0) {
			valid = get_string_value(value, self->values.ioprio);
		} else if (strcmp(name, "inhibit_screensaver") == 0) {
			valid = get_long_value(name, value, &self->values.inhibit_screensaver);
		} else if (strcmp(name, "disable_splitlock") == 0) {
			valid = get_long_value(name, value, &self->values.disable_splitlock);
		}
	} else if (strcmp(section, "gpu") == 0) {
		/* Protect the user - don't allow these config options from unsafe config locations */
		if (!load_protected) {
			LOG_ERROR(
			    "The [gpu] config section is not configurable from unsafe config files! Option %s "
			    "will be ignored!\n",
			    name);
			LOG_ERROR("Consider moving this option to /etc/gamemode.ini\n");
		}

		/* GPU subsection */
		if (strcmp(name, "apply_gpu_optimisations") == 0) {
			valid = get_string_value(value, self->values.apply_gpu_optimisations);
		} else if (strcmp(name, "gpu_device") == 0) {
			valid = get_long_value(name, value, &self->values.gpu_device);
		} else if (strcmp(name, "nv_core_clock_mhz_offset") == 0) {
			valid = get_long_value(name, value, &self->values.nv_core_clock_mhz_offset);
		} else if (strcmp(name, "nv_mem_clock_mhz_offset") == 0) {
			valid = get_long_value(name, value, &self->values.nv_mem_clock_mhz_offset);
		} else if (strcmp(name, "nv_powermizer_mode") == 0) {
			valid = get_long_value(name, value, &self->values.nv_powermizer_mode);
		} else if (strcmp(name, "amd_performance_level") == 0) {
			valid = get_string_value(value, self->values.amd_performance_level);
		}
	} else if (strcmp(section, "cpu") == 0) {
		if (strcmp(name, "park_cores") == 0) {
			valid = get_string_value(value, self->values.cpu_park_cores);
		} else if (strcmp(name, "pin_cores") == 0) {
			valid = get_string_value(value, self->values.cpu_pin_cores);
		}
	} else if (strcmp(section, "supervisor") == 0) {
		/* Supervisor subsection */
		if (strcmp(name, "supervisor_whitelist") == 0) {
			valid = append_value_to_list(name, value, self->values.supervisor_whitelist);
		} else if (strcmp(name, "supervisor_blacklist") == 0) {
			valid = append_value_to_list(name, value, self->values.supervisor_blacklist);
		} else if (strcmp(name, "require_supervisor") == 0) {
			valid = get_long_value(name, value, &self->values.require_supervisor);
		}
	} else if (strcmp(section, "custom") == 0) {
		/* Custom subsection */
		if (strcmp(name, "start") == 0) {
			valid = append_value_to_list(name, value, self->values.startscripts);
		} else if (strcmp(name, "end") == 0) {
			valid = append_value_to_list(name, value, self->values.endscripts);
		} else if (strcmp(name, "script_timeout") == 0) {
			valid = get_long_value(name, value, &self->values.script_timeout);
		}
	}

	if (!valid) {
		/* Simply ignore the value, but with a log */
		LOG_MSG("Config: Value ignored [%s] %s=%s\n", section, name, value);
	}

	return 1;
}

/*
 * Load the config file
 */
static void load_config_files(GameModeConfig *self)
{
	/* grab the current dir */
	char *config_location_local = get_current_dir_name();

	/* Get home config location */
	char *config_location_home = NULL;
	const char *cfg = getenv("XDG_CONFIG_HOME");
	if (cfg) {
		config_location_home = realpath(cfg, NULL);
	} else {
		cfg = getenv("HOME");
		if (cfg) {
			char *cfg_full = NULL;
			if (asprintf(&cfg_full, "%s/.config", cfg) > 0) {
				config_location_home = realpath(cfg_full, NULL);
				free(cfg_full);
			}
		} else {
			struct passwd *p = getpwuid(getuid());
			if (p) {
				config_location_home = realpath(p->pw_dir, NULL);
			}
		}
	}

	/* Take the write lock for the internal data */
	pthread_rwlock_wrlock(&self->rwlock);

	/* Clear our config values */
	memset(&self->values, 0, sizeof(self->values));

	/* Set some non-zero defaults */
	self->values.igpu_power_threshold = DEFAULT_IGPU_POWER_THRESHOLD;
	self->values.inhibit_screensaver = 1; /* Defaults to on */
	self->values.disable_splitlock = 1;   /* Defaults to on */
	self->values.reaper_frequency = DEFAULT_REAPER_FREQ;
	self->values.gpu_device = 0;
	self->values.nv_powermizer_mode = -1;
	self->values.nv_core_clock_mhz_offset = -1;
	self->values.nv_mem_clock_mhz_offset = -1;
	self->values.script_timeout = 10; /* Default to 10 seconds for scripts */

	/*
	 * Locations to load, in order
	 * Arrays merge and values overwrite
	 */
	struct ConfigLocation {
		const char *path;
		bool protected;
	};
	struct ConfigLocation locations[CONFIG_NUM_LOCATIONS] = {
		{ SYSCONFDIR, true },            /* shipped default config */
		{ "/etc", true },                /* administrator config */
		{ config_location_home, false }, /* $XDG_CONFIG_HOME or $HOME/.config/ */
		{ config_location_local, false } /* local data eg. $PWD */
	};

	/* Load each file in order and overwrite values */
	for (unsigned int i = 0; i < CONFIG_NUM_LOCATIONS; i++) {
		char *path = NULL;
		if (locations[i].path && asprintf(&path, "%s/" CONFIG_NAME, locations[i].path) > 0) {
			FILE *f = NULL;
			DIR *d = NULL;
			if ((f = fopen(path, "r"))) {
				LOG_MSG("Loading config file [%s]\n", path);
				load_protected = locations[i].protected;
				int error = ini_parse_file(f, inih_handler, (void *)self);

				/* Failure here isn't fatal */
				if (error) {
					LOG_MSG("Failed to parse config file - error on line %d!\n", error);
				}
				fclose(f);

				/* Register for inotify */
				/* Watch for modification, deletion, moves, or attribute changes */
				uint32_t fileflags = IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF;
				if ((self->inotwd[i] = inotify_add_watch(self->inotfd, path, fileflags)) == -1) {
					LOG_ERROR("Failed to watch %s, error: %s", path, strerror(errno));
				}

			} else if ((d = opendir(locations[i].path))) {
				/* We didn't find a file, so we'll wait on the directory */
				/* Notify if a file is created, or move to the directory, or if the directory itself
				 * is removed or moved away */
				uint32_t dirflags = IN_CREATE | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
				if ((self->inotwd[i] =
				         inotify_add_watch(self->inotfd, locations[i].path, dirflags)) == -1) {
					LOG_ERROR("Failed to watch %s, error: %s", path, strerror(errno));
				}
				closedir(d);
			}
			free(path);
		}
	}

	/* clean up memory */
	free(config_location_home);
	free(config_location_local);

	/* Release the lock */
	pthread_rwlock_unlock(&self->rwlock);
}

/*
 * Copy a config parameter with a lock
 */
static void memcpy_locked_config(GameModeConfig *self, void *dst, void *src, size_t n)
{
	/* Take the read lock */
	pthread_rwlock_rdlock(&self->rwlock);

	/* copy the data */
	memcpy(dst, src, n);

	/* release the lock */
	pthread_rwlock_unlock(&self->rwlock);
}

/*
 * Create a context object
 */
GameModeConfig *config_create(void)
{
	GameModeConfig *newconfig = (GameModeConfig *)malloc(sizeof(GameModeConfig));

	return newconfig;
}

/*
 * Initialise the config
 */
void config_init(GameModeConfig *self)
{
	pthread_rwlock_init(&self->rwlock, NULL);

	self->inotfd = inotify_init1(IN_NONBLOCK);
	if (self->inotfd == -1)
		LOG_ERROR(
		    "inotify_init failed: %s, gamemode will not be able to watch config files for edits!\n",
		    strerror(errno));

	for (unsigned int i = 0; i < CONFIG_NUM_LOCATIONS; i++) {
		self->inotwd[i] = -1;
	}

	/* load the initial config */
	load_config_files(self);
}

/*
 * Destroy internal parts of config
 */
static void internal_destroy(GameModeConfig *self)
{
	pthread_rwlock_destroy(&self->rwlock);

	for (unsigned int i = 0; i < CONFIG_NUM_LOCATIONS; i++) {
		if (self->inotwd[i] != -1) {
			/* TODO: Error handle */
			inotify_rm_watch(self->inotfd, self->inotwd[i]);
		}
	}

	if (self->inotfd != -1)
		close(self->inotfd);
}

/*
 * Re-load the config file
 */
void config_reload(GameModeConfig *self)
{
	internal_destroy(self);

	config_init(self);
}

/*
 * Check if the config needs to be reloaded
 */
bool config_needs_reload(GameModeConfig *self)
{
	bool need = false;

	/* Take a read lock while we use the inotify fd */
	pthread_rwlock_rdlock(&self->rwlock);

	const size_t buflen = sizeof(struct inotify_event) + NAME_MAX + 1;
	char buffer[buflen] __attribute__((aligned(__alignof__(struct inotify_event))));

	ssize_t len = read(self->inotfd, buffer, buflen);
	if (len == -1) {
		/* EAGAIN is returned when there's nothing to read on a non-blocking fd */
		if (errno != EAGAIN)
			LOG_ERROR("Could not read inotify fd: %s\n", strerror(errno));
	} else if (len > 0) {
		/* Iterate over each event we've been given */
		size_t i = 0;
		while (i < (size_t)len) {
			struct inotify_event *event = (struct inotify_event *)&buffer[i];
			/* We have picked up an event and need to handle it */
			if (event->mask & IN_ISDIR) {
				/* If the event is a dir event we need to take a look */
				if (event->mask & IN_DELETE_SELF || event->mask & IN_MOVE_SELF) {
					/* The directory itself changed, trigger a reload */
					need = true;
					break;
				}

			} else {
				/* When the event has a filename (ie. is from a dir watch), check the name */
				if (event->len > 0) {
					if (strncmp(basename(event->name), CONFIG_NAME, strlen(CONFIG_NAME)) == 0) {
						/* This is a gamemode config file, trigger a reload */
						need = true;
						break;
					}
				} else {
					/* Otherwise this is for one of our watches on a specific config file, so
					 * trigger the reload regardless */
					need = true;
					break;
				}
			}

			i += sizeof(struct inotify_event) + event->len;
		}
	}

	/* Return the read lock */
	pthread_rwlock_unlock(&self->rwlock);

	return need;
}

/*
 * Destroy the config
 */
void config_destroy(GameModeConfig *self)
{
	internal_destroy(self);

	/* Finally, free the memory */
	free(self);
}

/*
 * Checks if the client is whitelisted
 */
bool config_get_client_whitelisted(GameModeConfig *self, const char *client)
{
	/* Take the read lock for the internal data */
	pthread_rwlock_rdlock(&self->rwlock);

	/* If the whitelist is empty then everything passes */
	bool found = true;
	if (self->values.whitelist[0][0]) {
		/*
		 * Check if the value is found in our whitelist
		 * Currently is a simple strstr check, but could be modified for wildcards etc.
		 */
		found = config_string_list_contains(client, self->values.whitelist);
	}

	/* release the lock */
	pthread_rwlock_unlock(&self->rwlock);
	return found;
}

/*
 * Checks if the client is blacklisted
 */
bool config_get_client_blacklisted(GameModeConfig *self, const char *client)
{
	/* Take the read lock for the internal data */
	pthread_rwlock_rdlock(&self->rwlock);

	/*
	 * Check if the value is found in our whitelist
	 * Currently is a simple strstr check, but could be modified for wildcards etc.
	 */
	bool found = config_string_list_contains(client, self->values.blacklist);

	/* release the lock */
	pthread_rwlock_unlock(&self->rwlock);
	return found;
}

/*
 * Gets the reaper frequency
 */
DEFINE_CONFIG_GET(reaper_frequency)

/*
 * Gets the screensaver inhibit setting
 */
bool config_get_inhibit_screensaver(GameModeConfig *self)
{
	long val;
	memcpy_locked_config(self, &val, &self->values.inhibit_screensaver, sizeof(long));
	return val == 1;
}

/*
 * Gets the disable splitlock setting
 */
bool config_get_disable_splitlock(GameModeConfig *self)
{
	long val;
	memcpy_locked_config(self, &val, &self->values.disable_splitlock, sizeof(long));
	return val == 1;
}

/*
 * Get a set of scripts to call when gamemode starts
 */
void config_get_gamemode_start_scripts(GameModeConfig *self,
                                       char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self,
	                     scripts,
	                     self->values.startscripts,
	                     sizeof(self->values.startscripts));
}

/*
 * Get a set of scripts to call when gamemode ends
 */
void config_get_gamemode_end_scripts(GameModeConfig *self,
                                     char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, scripts, self->values.endscripts, sizeof(self->values.startscripts));
}

/*
 * Get the script timemout value
 */
DEFINE_CONFIG_GET(script_timeout)

/*
 * Get the chosen default governor
 */
void config_get_default_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, governor, self->values.defaultgov, sizeof(self->values.defaultgov));
}

/*
 * Get the chosen desired governor
 */
void config_get_desired_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, governor, self->values.desiredgov, sizeof(self->values.desiredgov));
}

/*
 * Get the chosen iGPU desired governor
 */
void config_get_igpu_desired_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self,
	                     governor,
	                     self->values.igpu_desiredgov,
	                     sizeof(self->values.igpu_desiredgov));
}

/*
 * Get the chosen iGPU power threshold
 */
float config_get_igpu_power_threshold(GameModeConfig *self)
{
	float value = 0;
	memcpy_locked_config(self, &value, &self->values.igpu_power_threshold, sizeof(float));
	/* Validate the threshold value */
	if (isnan(value) || value < 0) {
		LOG_ONCE(ERROR,
		         "Configured iGPU power threshold value '%f' is invalid, ignoring iGPU default "
		         "governor.\n",
		         value);
		value = FP_INFINITE;
	}
	return value;
}

/*
 * Get the chosen soft realtime behavior
 */
void config_get_soft_realtime(GameModeConfig *self, char softrealtime[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self,
	                     softrealtime,
	                     self->values.softrealtime,
	                     sizeof(self->values.softrealtime));
}

/*
 * Get the renice value
 */
long config_get_renice_value(GameModeConfig *self)
{
	long value = 0;
	memcpy_locked_config(self, &value, &self->values.renice, sizeof(long));
	/* Validate the renice value */
	if ((value < 1 || value > 20) && value != 0) {
		LOG_ONCE(ERROR, "Configured renice value '%ld' is invalid, will not renice.\n", value);
		value = 0;
	}
	return value;
}

/*
 * Get the ioprio value
 */
long config_get_ioprio_value(GameModeConfig *self)
{
	long value = 0;
	char ioprio_value[CONFIG_VALUE_MAX] = { 0 };
	memcpy_locked_config(self, ioprio_value, &self->values.ioprio, sizeof(self->values.ioprio));

	/* account for special string values */
	if (0 == strncmp(ioprio_value, "off", sizeof(self->values.ioprio)))
		value = IOPRIO_DONT_SET;
	else if (0 == strncmp(ioprio_value, "default", sizeof(self->values.ioprio)))
		value = IOPRIO_RESET_DEFAULT;
	else
		value = atoi(ioprio_value);

	/* Validate values */
	if (IOPRIO_RESET_DEFAULT == value) {
		LOG_ONCE(MSG, "IO priority will be reset to default behavior (based on CPU priority).\n");
		value = 0;
	} else {
		/* maybe clamp the value */
		long invalid_ioprio = value;
		value = CLAMP(0, 7, value);
		if (value != invalid_ioprio)
			LOG_ONCE(ERROR,
			         "IO priority value %ld invalid, clamping to %ld\n",
			         invalid_ioprio,
			         value);
	}

	return value;
}

/*
 * Get various config info for gpu optimisations
 */
void config_get_apply_gpu_optimisations(GameModeConfig *self, char value[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self,
	                     value,
	                     &self->values.apply_gpu_optimisations,
	                     sizeof(self->values.apply_gpu_optimisations));
}

/* Define the getters for GPU values */
DEFINE_CONFIG_GET(gpu_device)
DEFINE_CONFIG_GET(nv_core_clock_mhz_offset)
DEFINE_CONFIG_GET(nv_mem_clock_mhz_offset)
DEFINE_CONFIG_GET(nv_powermizer_mode)

void config_get_amd_performance_level(GameModeConfig *self, char value[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self,
	                     value,
	                     &self->values.amd_performance_level,
	                     sizeof(self->values.amd_performance_level));
}

/*
        char supervisor_whitelist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
        char supervisor_blacklist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
*/
DEFINE_CONFIG_GET(require_supervisor)

/*
 * Get various config info for cpu optimisations
 */
void config_get_cpu_park_cores(GameModeConfig *self, char value[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self,
	                     value,
	                     &self->values.cpu_park_cores,
	                     sizeof(self->values.cpu_park_cores));
}

void config_get_cpu_pin_cores(GameModeConfig *self, char value[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self,
	                     value,
	                     &self->values.cpu_pin_cores,
	                     sizeof(self->values.cpu_pin_cores));
}

/*
 * Checks if the supervisor is whitelisted
 */
bool config_get_supervisor_whitelisted(GameModeConfig *self, const char *supervisor)
{
	/* Take the read lock for the internal data */
	pthread_rwlock_rdlock(&self->rwlock);

	/* If the whitelist is empty then everything passes */
	bool found = true;
	if (self->values.supervisor_whitelist[0][0]) {
		/*
		 * Check if the value is found in our whitelist
		 * Currently is a simple strstr check, but could be modified for wildcards etc.
		 */
		found = config_string_list_contains(supervisor, self->values.supervisor_whitelist);
	}

	/* release the lock */
	pthread_rwlock_unlock(&self->rwlock);
	return found;
}

/*
 * Checks if the supervisor is blacklisted
 */
bool config_get_supervisor_blacklisted(GameModeConfig *self, const char *supervisor)
{
	/* Take the read lock for the internal data */
	pthread_rwlock_rdlock(&self->rwlock);

	/*
	 * Check if the value is found in our whitelist
	 * Currently is a simple strstr check, but could be modified for wildcards etc.
	 */
	bool found = config_string_list_contains(supervisor, self->values.supervisor_blacklist);

	/* release the lock */
	pthread_rwlock_unlock(&self->rwlock);
	return found;
}
