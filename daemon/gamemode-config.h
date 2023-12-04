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
#pragma once

#include <stdbool.h>

/*
 * Maximum sizes values in a config list
 * In practice inih has a INI_MAX_LINE value of 200 so the length is just a safeguard
 */
#define CONFIG_LIST_MAX 32
#define CONFIG_VALUE_MAX 256

/*
 * Special ioprio values
 */
#define IOPRIO_RESET_DEFAULT -1
#define IOPRIO_DONT_SET -2
#define IOPRIO_DEFAULT 4

/*
 * Opaque config context type
 */
typedef struct GameModeConfig GameModeConfig;

/*
 * Initialise a config
 */
GameModeConfig *config_create(void);

/*
 * Initialise a config
 * Must be called before using any config later config functions
 */
void config_init(GameModeConfig *self);

/*
 * Reload a config from disk
 * Thread safe to call
 */
void config_reload(GameModeConfig *self);

/*
 * Check if the config has changed and will need a reload
 */
bool config_needs_reload(GameModeConfig *self);

/*
 * Destroy a config
 * Invalidates the config
 */
void config_destroy(GameModeConfig *self);

/*
 * Get if the client is in the whitelist or blacklist
 * config_get_client_whitelisted returns false for an empty whitelist
 */
bool config_get_client_whitelisted(GameModeConfig *self, const char *client);
bool config_get_client_blacklisted(GameModeConfig *self, const char *client);

/*
 * Get the script sets to run at the start or end
 */
void config_get_gamemode_start_scripts(GameModeConfig *self,
                                       char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX]);
void config_get_gamemode_end_scripts(GameModeConfig *self,
                                     char scripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX]);

/*
 * Various get methods for config values
 */
long config_get_reaper_frequency(GameModeConfig *self);
bool config_get_inhibit_screensaver(GameModeConfig *self);
long config_get_script_timeout(GameModeConfig *self);
void config_get_default_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX]);
void config_get_desired_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX]);
void config_get_igpu_desired_governor(GameModeConfig *self, char governor[CONFIG_VALUE_MAX]);
float config_get_igpu_power_threshold(GameModeConfig *self);
void config_get_soft_realtime(GameModeConfig *self, char softrealtime[CONFIG_VALUE_MAX]);
long config_get_renice_value(GameModeConfig *self);
long config_get_ioprio_value(GameModeConfig *self);
bool config_get_disable_splitlock(GameModeConfig *self);

/*
 * Get various config info for gpu optimisations
 */
void config_get_apply_gpu_optimisations(GameModeConfig *self, char value[CONFIG_VALUE_MAX]);
long config_get_gpu_device(GameModeConfig *self);
long config_get_nv_core_clock_mhz_offset(GameModeConfig *self);
long config_get_nv_mem_clock_mhz_offset(GameModeConfig *self);
long config_get_nv_powermizer_mode(GameModeConfig *self);
void config_get_amd_performance_level(GameModeConfig *self, char value[CONFIG_VALUE_MAX]);

/*
 * Get various config info for cpu optimisations
 */
void config_get_cpu_park_cores(GameModeConfig *self, char value[CONFIG_VALUE_MAX]);
void config_get_cpu_pin_cores(GameModeConfig *self, char value[CONFIG_VALUE_MAX]);

/**
 * Functions to get supervisor config permissions
 */
long config_get_require_supervisor(GameModeConfig *self);
bool config_get_supervisor_whitelisted(GameModeConfig *self, const char *supervisor);
bool config_get_supervisor_blacklisted(GameModeConfig *self, const char *supervisor);
