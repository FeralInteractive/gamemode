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
#include <sys/types.h>

#define INVALID_PROCFD -1

typedef int procfd_t;

/**
 * Opaque types
 */
typedef struct GameModeContext GameModeContext;
typedef struct GameModeConfig GameModeConfig;

/**
 * Return the singleton instance
 */
GameModeContext *game_mode_context_instance(void);

/**
 * Initialise the GameModeContext
 *
 * This is performed in a thread-safe fashion.
 */
void game_mode_context_init(GameModeContext *self);

/**
 * Destroy the previously initialised GameModeContext.
 *
 * This is performed in a thread safe fashion.
 */
void game_mode_context_destroy(GameModeContext *self);

/**
 * Query the number of currently registered clients.
 *
 * @returns The number of clients. A number > 0 means that gamemode is active.
 */
int game_mode_context_num_clients(GameModeContext *self);

/**
 * Register a new game client with the context
 *
 * @param pid Process ID for the remote client
 * @param requester Process ID for the remote requestor
 * @returns 0 if the request was accepted and the client could be registered
 *          -1 if the request was accepted but the client could not be registered
 *          -2 if the request was rejected
 */
int game_mode_context_register(GameModeContext *self, pid_t pid, pid_t requester);

/**
 * Unregister an existing remote game client from the context
 *
 * @param pid Process ID for the remote client
 * @param requester Process ID for the remote requestor
 * @returns 0 if the request was accepted and the client existed
 *          -1 if the request was accepted but the client did not exist
 *          -2 if the request was rejected
 */
int game_mode_context_unregister(GameModeContext *self, pid_t pid, pid_t requester);

/**
 * Query the current status of gamemode
 *
 * @param pid Process ID for the remote client
 * @returns Positive if gamemode is active
 *          1 if gamemode is active but the client is not registered
 *          2 if gamemode is active and the client is registered
 *          -2 if this requester was rejected
 */
int game_mode_context_query_status(GameModeContext *self, pid_t pid, pid_t requester);

/**
 * Query the config of a gamemode context
 *
 * @param context A gamemode context
 * @returns Configuration from the gamemode context
 */
GameModeConfig *game_mode_config_from_context(const GameModeContext *context);

/** gamemode-env.c
 * Provides internal API functions specific to working environment
 * variables.
 */
char *game_mode_lookup_proc_env(const procfd_t proc_fd, const char *var);
char *game_mode_lookup_user_home(void);

/** gamemode-ioprio.c
 * Provides internal API functions specific to adjusting process
 * IO priorities.
 */
void game_mode_apply_ioprio(const GameModeContext *self, const pid_t client);

/** gamemode-proc.c
 * Provides internal API functions specific to working with process
 * environments.
 */
procfd_t game_mode_open_proc(const pid_t pid);
int game_mode_close_proc(const procfd_t procfd);

/** gamemode-sched.c
 * Provides internal API functions specific to adjusting process
 * scheduling.
 */
void game_mode_apply_renice(const GameModeContext *self, const pid_t client);
void game_mode_apply_scheduling(const GameModeContext *self, const pid_t client);

/** gamemode-wine.c
 * Provides internal API functions specific to handling wine
 * prefixes.
 */
bool game_mode_detect_wine_loader(const char *exe);
bool game_mode_detect_wine_preloader(const char *exe);
char *game_mode_resolve_wine_preloader(const pid_t pid);

/** gamemode-tests.c
 * Provides a test suite to verify gamemode behaviour
 */
int game_mode_run_client_tests(void);

/** gamemode-gpu.c
 * Provides internal APU functions to apply optimisations to gpus
 */
typedef struct GameModeGPUInfo GameModeGPUInfo;
int game_mode_initialise_gpu(GameModeConfig *config, GameModeGPUInfo **info);
void game_mode_free_gpu(GameModeGPUInfo **info);
int game_mode_apply_gpu(const GameModeGPUInfo *info);
int game_mode_get_gpu(GameModeGPUInfo *info);
