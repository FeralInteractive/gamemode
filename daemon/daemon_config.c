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

#include "daemon_config.h"
#include "logging.h"

/* Ben Hoyt's inih library */
#include "ini.h"

#include <linux/limits.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

/* Name and possible location of the config file */
#define CONFIG_NAME "gamemode.ini"

/* Default value for the reaper frequency */
#define DEFAULT_REAPER_FREQ 5

/**
 * The config holds various details as needed
 * and a rwlock to allow config_reload to be called
 */
struct GameModeConfig {
	pthread_rwlock_t rwlock;
	int inotfd;
	int inotwd;

	char whitelist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	char blacklist[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];

	char startscripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	char endscripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];

	char defaultgov[CONFIG_VALUE_MAX];
	char desiredgov[CONFIG_VALUE_MAX];

	char softrealtime[CONFIG_VALUE_MAX];
	long renice;

	char ioprio[CONFIG_VALUE_MAX];

	long inhibit_screensaver;

	long reaper_frequency;
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
 * Get a positive long value from a string
 */
static bool get_long_value(const char *value_name, const char *value, long *output)
{
	char *end = NULL;
	long config_value = strtol(value, &end, 10);

	if (errno == ERANGE) {
		LOG_ERROR("Config: %s overflowed, given [%s]\n", value_name, value);
		return false;
	} else if (config_value <= 0 || !(*value != '\0' && end && *end == '\0')) {
		LOG_ERROR("Config: %s was invalid, given [%s]\n", value_name, value);
		return false;
	} else {
		*output = config_value;
	}

	return true;
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
			valid = append_value_to_list(name, value, self->whitelist);
		} else if (strcmp(name, "blacklist") == 0) {
			valid = append_value_to_list(name, value, self->blacklist);
		}
	} else if (strcmp(section, "general") == 0) {
		/* General subsection */
		if (strcmp(name, "reaper_freq") == 0) {
			valid = get_long_value(name, value, &self->reaper_frequency);
		} else if (strcmp(name, "defaultgov") == 0) {
			valid = get_string_value(value, self->defaultgov);
		} else if (strcmp(name, "desiredgov") == 0) {
			valid = get_string_value(value, self->desiredgov);
		} else if (strcmp(name, "softrealtime") == 0) {
			valid = get_string_value(value, self->softrealtime);
		} else if (strcmp(name, "renice") == 0) {
			valid = get_long_value(name, value, &self->renice);
		} else if (strcmp(name, "ioprio") == 0) {
			valid = get_string_value(value, self->ioprio);
		} else if (strcmp(name, "inhibit_screensaver") == 0) {
			valid = get_long_value(name, value, &self->inhibit_screensaver);
		}
	} else if (strcmp(section, "custom") == 0) {
		/* Custom subsection */
		if (strcmp(name, "start") == 0) {
			valid = append_value_to_list(name, value, self->startscripts);
		} else if (strcmp(name, "end") == 0) {
			valid = append_value_to_list(name, value, self->endscripts);
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
	memset(self->ioprio, 0, sizeof(self->ioprio));
	memset(self->whitelist, 0, sizeof(self->whitelist));
	memset(self->blacklist, 0, sizeof(self->blacklist));
	memset(self->startscripts, 0, sizeof(self->startscripts));
	memset(self->endscripts, 0, sizeof(self->endscripts));
	memset(self->defaultgov, 0, sizeof(self->defaultgov));
	memset(self->desiredgov, 0, sizeof(self->desiredgov));
	memset(self->softrealtime, 0, sizeof(self->softrealtime));
	self->renice = 4; /* default value of 4 */
	self->reaper_frequency = DEFAULT_REAPER_FREQ;
	self->inhibit_screensaver = 1; /* Defaults to on */

	/*
	 * Locations to load, in order
	 * Arrays merge and values overwrite
	 */
	const char *locations[] = {
		"/usr/share/gamemode", /* shipped default config */
		"/etc",                /* administrator config */
		config_location_home,  /* user defined config eg. $XDG_CONFIG_HOME or $HOME/.config/ */
		config_location_local  /* local data eg. $PWD */
	};

	/* Load each file in order and overwrite values */
	for (unsigned int i = 0; i < sizeof(locations) / sizeof(locations[0]); i++) {
		char *path = NULL;
		if (locations[i] && asprintf(&path, "%s/" CONFIG_NAME, locations[i]) > 0) {
			FILE *f = fopen(path, "r");
			if (f) {
				LOG_MSG("Loading config file [%s]\n", path);
				int error = ini_parse_file(f, inih_handler, (void *)self);

				/* Failure here isn't fatal */
				if (error) {
					LOG_MSG("Failed to parse config file - error on line %d!\n", error);
				}
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

	/* load the initial config */
	load_config_files(self);
}

/*
 * Re-load the config file
 */
void config_reload(GameModeConfig *self)
{
	load_config_files(self);
}

/*
 * Destroy the config
 */
void config_destroy(GameModeConfig *self)
{
	pthread_rwlock_destroy(&self->rwlock);

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
	if (self->whitelist[0][0]) {
		/*
		 * Check if the value is found in our whitelist
		 * Currently is a simple strstr check, but could be modified for wildcards etc.
		 */
		found = false;
		for (unsigned int i = 0; i < CONFIG_LIST_MAX && self->whitelist[i][0]; i++) {
			if (strstr(client, self->whitelist[i])) {
				found = true;
			}
		}
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
	bool found = false;
	for (unsigned int i = 0; i < CONFIG_LIST_MAX && self->blacklist[i][0]; i++) {
		if (strstr(client, self->blacklist[i])) {
			found = true;
		}
	}

	/* release the lock */
	pthread_rwlock_unlock(&self->rwlock);
	return found;
}

/*
 * Gets the reaper frequency
 */
void config_get_reaper_thread_frequency(GameModeConfig *self, long *value)
{
	memcpy_locked_config(self, value, &self->reaper_frequency, sizeof(long));
}

/*
 * Gets the screensaver inhibit setting
 */
bool config_get_inhibit_screensaver(GameModeConfig *self)
{
	long val;
	memcpy_locked_config(self, &val, &self->inhibit_screensaver, sizeof(long));
	return val == 1;
}

/*
 * Get a set of scripts to call when gamemode starts
 */
void config_get_gamemode_start_scripts(GameModeConfig *self,
                                       char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, scripts, self->startscripts, sizeof(self->startscripts));
}

/*
 * Get a set of scripts to call when gamemode ends
 */
void config_get_gamemode_end_scripts(GameModeConfig *self,
                                     char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, scripts, self->endscripts, sizeof(self->startscripts));
}

/*
 * Get the chosen default governor
 */
void config_get_default_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, governor, self->defaultgov, sizeof(self->defaultgov));
}

/*
 * Get the chosen desired governor
 */
void config_get_desired_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, governor, self->desiredgov, sizeof(self->desiredgov));
}

/*
 * Get the chosen soft realtime behavior
 */
void config_get_soft_realtime(GameModeConfig *self, char softrealtime[CONFIG_VALUE_MAX])
{
	memcpy_locked_config(self, softrealtime, self->softrealtime, sizeof(self->softrealtime));
}

/*
 * Get the renice value
 */
void config_get_renice_value(GameModeConfig *self, long *value)
{
	memcpy_locked_config(self, value, &self->renice, sizeof(long));
}

/*
 * Get the ioprio value
 */
void config_get_ioprio_value(GameModeConfig *self, int *value)
{
	char ioprio_value[CONFIG_VALUE_MAX] = { 0 };
	memcpy_locked_config(self, ioprio_value, &self->ioprio, sizeof(self->ioprio));
	if (0 == strncmp(ioprio_value, "off", sizeof(self->ioprio)))
		*value = IOPRIO_DONT_SET;
	else if (0 == strncmp(ioprio_value, "default", sizeof(self->ioprio)))
		*value = IOPRIO_RESET_DEFAULT;
	else
		*value = atoi(ioprio_value);
}
