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

#pragma once

#include <stdbool.h>
#include <sys/types.h>

/**
 * Opaque context
 */
typedef struct GameModeContext GameModeContext;

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
 * Register a new game client with the context
 *
 * @param pid Process ID for the remote client
 * @returns True if the new client could be registered
 */
bool game_mode_context_register(GameModeContext *self, pid_t pid);

/**
 * Unregister an existing remote game client from the context
 *
 * @param pid Process ID for the remote client
 * @returns True if the client was removed, and existed.
 */
bool game_mode_context_unregister(GameModeContext *self, pid_t pid);
