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
#include <stdint.h>
#include <sys/types.h>

#define INVALID_PROCFD -1

typedef int procfd_t;

/**
 * Opaque types
 */
typedef struct GameModeContext GameModeContext;
typedef struct GameModeConfig GameModeConfig;
typedef struct GameModeClient GameModeClient;

/**
 * GameModeClient related functions
 */

/**
 * Decrement the usage count of client.
 */
void game_mode_client_unref(GameModeClient *client);

/**
 * Increment the usage count of client.
 */
void game_mode_client_ref(GameModeClient *client);

/**
 * The process identifier of the client.
 */
pid_t game_mode_client_get_pid(GameModeClient *client);

/**
 * The path to the executable of client.
 */
const char *game_mode_client_get_executable(GameModeClient *client);

/**
 * The process identifier of the requester.
 */
pid_t game_mode_client_get_requester(GameModeClient *client);

/**
 * The time that game mode was requested for the client.
 */
u_int64_t game_mode_client_get_timestamp(GameModeClient *client);

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
 * List the currently active clients.
 * @param out holds the number of active clients.
 *
 * @returns A array of pid_t or NULL if there are no active clients.
 */
pid_t *game_mode_context_list_clients(GameModeContext *self, unsigned int *count);

/**
 * Lookup up information about a client via the pid;
 *
 * @returns A pointer to a GameModeClient struct or NULL in case no client
 *           with the corresponding id could be found. Adds a reference to
 *           GameModeClient that needs to be released.
 */
GameModeClient *game_mode_context_lookup_client(GameModeContext *self, pid_t client);
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

/*
 * Reload the current configuration
 */
int game_mode_reload_config(GameModeContext *context);

/** gamemode-ioprio.c
 * Provides internal API functions specific to adjusting process
 * IO priorities.
 */
int game_mode_get_ioprio(const pid_t client);
void game_mode_apply_ioprio(const GameModeContext *self, const pid_t client, int expected);

/** gamemode-sched.c
 * Provides internal API functions specific to adjusting process
 * scheduling.
 */
int game_mode_get_renice(const pid_t client);
void game_mode_apply_renice(const GameModeContext *self, const pid_t client, int expected);
void game_mode_apply_scheduling(const GameModeContext *self, const pid_t client);

/** gamemode-wine.c
 * Provides internal API functions specific to handling wine
 * prefixes.
 */
char *game_mode_resolve_wine_preloader(const char *exe, const pid_t pid);

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

/** gamemode-dbus.c
 * Provides an API interface for using dbus
 */
void game_mode_context_loop(GameModeContext *context) __attribute__((noreturn));
int game_mode_inhibit_screensaver(bool inhibit);
void game_mode_client_registered(pid_t);
void game_mode_client_unregistered(pid_t);
