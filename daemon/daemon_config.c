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
#include <stdio.h>
#include <string.h>

/* Name and possible location of the config file */
#define CONFIG_NAME "gamemode.ini"
#define CONFIG_DIR "/usr/share/gamemode/"

/* Maximum values in the whilelist and blacklist */
#define MAX_LIST_VALUES 32

/* Maximum length of values in the whilelist or blacklist */
#define MAX_LIST_VALUE_LENGTH 256

/**
 * The config holds various details as needed
 * and a rwlock to allow config_reload to be called
 */
struct GameModeConfig {
	pthread_rwlock_t rwlock;
	int inotfd;
	int inotwd;

	char whitelist[MAX_LIST_VALUES][MAX_LIST_VALUE_LENGTH];
	char blacklist[MAX_LIST_VALUES][MAX_LIST_VALUE_LENGTH];
};

/*
 * Handler for the inih callback
 */
static int inih_handler(void *user, const char *section, const char *name, const char *value)
{
	GameModeConfig *self = (GameModeConfig *)user;
	bool valid = false;

	/* Filter subsection */
	if (strcmp(section, "filter") == 0) {
		if (strcmp(name, "whitelist") == 0) {
			valid = true;

			unsigned int i = 0;
			while (*self->whitelist[i] && ++i < MAX_LIST_VALUES)
				;

			if (i < MAX_LIST_VALUES) {
				strncpy(self->whitelist[i], value, MAX_LIST_VALUE_LENGTH);
			} else {
				LOG_MSG("Could not add [%s] to the whitelist, exceeds limit of %d\n",
				        value,
				        MAX_LIST_VALUES);
			}
		} else if (strcmp(name, "blacklist") == 0) {
			valid = true;

			unsigned int i = 0;
			while (*self->blacklist[i] && ++i < MAX_LIST_VALUES)
				;

			if (i < MAX_LIST_VALUES) {
				strncpy(self->blacklist[i], value, MAX_LIST_VALUE_LENGTH);
			} else {
				LOG_MSG("Could not add [%s] to the blacklist, exceeds limit of %d\n",
				        value,
				        MAX_LIST_VALUES);
			}
		}
	}

	if (!valid) {
		/* We hit an unknown section succeed but with a log */
		LOG_MSG("Unknown value in config file [%s] %s=%s\n", section, name, value);
	}

	return 1;
}

/*
 * Load the config file
 */
static void load_config_file(GameModeConfig *self)
{
	/* Take the write lock for the internal data */
	pthread_rwlock_wrlock(&self->rwlock);

	/* Clear our config values */
	memset(self->whitelist, 0, sizeof(self->whitelist));
	memset(self->blacklist, 0, sizeof(self->blacklist));

	/* try locally first */
	FILE *f = fopen(CONFIG_NAME, "r");
	if (!f) {
		f = fopen(CONFIG_DIR CONFIG_NAME, "r");
		if (!f) {
			/* Failure here isn't fatal */
			LOG_ERROR("Note: No config file found [%s] in working directory or in [%s]\n",
			          CONFIG_NAME,
			          CONFIG_DIR);
		}
	}

	if (f) {
		int error = ini_parse_file(f, inih_handler, (void *)self);

		/* Failure here isn't fatal */
		if (error) {
			LOG_MSG("Failed to parse config file - error on line %d!\n", error);
		}

		fclose(f);
	}

	/* Release the lock */
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
	load_config_file(self);
}

/*
 * Re-load the config file
 */
void config_reload(GameModeConfig *self)
{
	load_config_file(self);
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
		for (unsigned int i = 0; i < MAX_LIST_VALUES && self->whitelist[i][0]; i++) {
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
	for (unsigned int i = 0; i < MAX_LIST_VALUES && self->blacklist[i][0]; i++) {
		if (strstr(client, self->blacklist[i])) {
			found = true;
		}
	}

	/* release the lock */
	pthread_rwlock_unlock(&self->rwlock);
	return found;
}
