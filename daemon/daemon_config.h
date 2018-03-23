/*

Copyright (c) 2017, Feral Interactive
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
 * Destroy a config
 * Invalidates the config
 */
void config_destroy(GameModeConfig *self);

/*
 * Get if the client is in the whitelist
 * returns false for an empty whitelist
 */
bool config_get_client_whitelisted(GameModeConfig *self, const char *client);

/*
 * Get if the client is in the blacklist
 */
bool config_get_client_blacklisted(GameModeConfig *self, const char *client);
