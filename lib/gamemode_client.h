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
#ifndef CLIENT_GAMEMODE_H
#define CLIENT_GAMEMODE_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

static char _client_error_string[512] = { 0 };

/**
 * Load libgamemode dynamically to dislodge us from most dependencies.
 * This allows clients to link and/or use this regardless of runtime.
 * See SDL2 for an example of the reasoning behind this in terms of
 * dynamic versioning as well.
 */
static volatile int _libgamemode_loaded = 1;

/* Typedefs for the functions to load */
typedef int (*_gamemode_request_start)(void);
typedef int (*_gamemode_request_end)(void);
typedef const char *(*_gamemode_error_string)(void);

/* Storage for functors */
static _gamemode_request_start _REAL_gamemode_request_start = NULL;
static _gamemode_request_end _REAL_gamemode_request_end = NULL;
static _gamemode_error_string _REAL_gamemode_error_string = NULL;

/**
 * Internal helper to perform the symbol binding safely.
 *
 * Returns 0 on success and -1 on failure
 */
__attribute__((always_inline)) static inline int _bind_libgamemode_symbol(void *handle,
                                                                          const char *name,
                                                                          void **out_func,
                                                                          size_t func_size)
{
	void *symbol_lookup = NULL;
	char *dl_error = NULL;

	/* Safely look up the symbol */
	symbol_lookup = dlsym(handle, name);
	dl_error = dlerror();
	if (dl_error || !symbol_lookup) {
		snprintf(_client_error_string, sizeof(_client_error_string), "dlsym failed - %s", dl_error);
		return -1;
	}

	/* Have the symbol correctly, copy it to make it usable */
	memcpy(out_func, &symbol_lookup, func_size);
	return 0;
}

/**
 * Loads libgamemode and needed functions
 *
 * Returns 0 on success and -1 on failure
 */
__attribute__((always_inline)) static inline int _load_libgamemode(void)
{
	/* We start at 1, 0 is a success and -1 is a fail */
	if (_libgamemode_loaded != 1) {
		return _libgamemode_loaded;
	}

	/* Anonymous struct type to define our bindings */
	struct binding {
		const char *name;
		void **functor;
		size_t func_size;
	} bindings[] = {
		{ "real_gamemode_request_start",
		  (void **)&_REAL_gamemode_request_start,
		  sizeof(_REAL_gamemode_request_start) },
		{ "real_gamemode_request_end",
		  (void **)&_REAL_gamemode_request_end,
		  sizeof(_REAL_gamemode_request_end) },
		{ "real_gamemode_error_string",
		  (void **)&_REAL_gamemode_error_string,
		  sizeof(_REAL_gamemode_error_string) },
	};

	void *libgamemode = NULL;

	/* Try and load libgamemode */
	libgamemode = dlopen("libgamemode.so", RTLD_NOW);
	if (!libgamemode) {
		snprintf(_client_error_string,
		         sizeof(_client_error_string),
		         "dylopen failed - %s",
		         dlerror());
		_libgamemode_loaded = -1;
		return -1;
	}

	/* Attempt to bind all symbols */
	for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
		struct binding *binder = &bindings[i];

		if (_bind_libgamemode_symbol(libgamemode,
		                             binder->name,
		                             binder->functor,
		                             binder->func_size) != 0) {
			_libgamemode_loaded = -1;
			return -1;
		};
	}

	/* Success */
	_libgamemode_loaded = 0;
	return 0;
}

/**
 * Redirect to the real libgamemode
 */
__attribute__((always_inline)) static inline const char *gamemode_error_string(void)
{
	/* If we fail to load the system gamemode, return our error string */
	if (_load_libgamemode() < 0) {
		return _client_error_string;
	}

	return _REAL_gamemode_error_string();
}

/**
 * Redirect to the real libgamemod
 * Allow automatically requesting game mode
 * Also prints errors as they happen.
 */
#ifdef GAMEMODE_AUTO
__attribute__((constructor))
#else
__attribute__((always_inline)) static inline
#endif
int gamemode_request_start(void)
{
	/* Need to load gamemode */
	if (_load_libgamemode() < 0) {
#ifdef GAMEMODE_AUTO
		fprintf(stderr, "gamemodeauto: %s\n", gamemode_error_string());
#endif
		return -1;
	}

	if (_REAL_gamemode_request_start() < 0) {
#ifdef GAMEMODE_AUTO
		fprintf(stderr, "gamemodeauto: %s\n", gamemode_error_string());
#endif
		return -1;
	}

	return 0;
}

/* Redirect to the real libgamemode */
#ifdef GAMEMODE_AUTO
__attribute__((destructor))
#else
__attribute__((always_inline)) static inline
#endif
int gamemode_request_end(void)
{
	/* Need to load gamemode */
	if (_load_libgamemode() < 0) {
#ifdef GAMEMODE_AUTO
		fprintf(stderr, "gamemodeauto: %s\n", gamemode_error_string());
#endif
		return -1;
	}

	if (_REAL_gamemode_request_end() < 0) {
#ifdef GAMEMODE_AUTO
		fprintf(stderr, "gamemodeauto: %s\n", gamemode_error_string());
#endif
		return -1;
	}

	return 0;
}

#endif // CLIENT_GAMEMODE_H
