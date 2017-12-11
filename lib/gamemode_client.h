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
#ifndef _CLIENT_GAMEMODE_H_
#define _CLIENT_GAMEMODE_H_
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <dlfcn.h>
#include <errno.h>
#include <string.h>

char _client_error_string[512] = {};

// Load libgamemode dynamically to dislodge us from most dependencies
// This allows clients to link and/or use this regardless of runtime
// See SDL2 for an example of the reasoning behind this in terms of
// dynamic versioning as well
int _libgamemode_loaded = 1;

// Typedefs for the functions to load
typedef int(*_gamemode_request_start)();
typedef int(*_gamemode_request_end)();
typedef const char*(*_gamemode_error_string)();

// Storage for functors
_gamemode_request_start _REAL_gamemode_request_start = NULL;
_gamemode_request_end   _REAL_gamemode_request_end   = NULL;
_gamemode_error_string  _REAL_gamemode_error_string  = NULL;

// Loads libgamemode and needed functions
// returns 0 on success and -1 on failure
__attribute__((always_inline))
inline int _load_libgamemode()
{
	// We start at 1, 0 is a success and -1 is a fail
	if ( _libgamemode_loaded != 1 )
		return _libgamemode_loaded;

	void* libgamemode = NULL;

	// Try and load libgamemode
	libgamemode = dlopen( "libgamemode.so", RTLD_NOW );
	if( !libgamemode )
		snprintf( _client_error_string, sizeof(_client_error_string), "dylopen failed - %s", dlerror() );
	else
	{
		_REAL_gamemode_request_start = (_gamemode_request_start)dlsym( libgamemode, "real_gamemode_request_start" );
		_REAL_gamemode_request_end   = (_gamemode_request_end)  dlsym( libgamemode, "real_gamemode_request_end" );
		_REAL_gamemode_error_string  = (_gamemode_error_string) dlsym( libgamemode, "real_gamemode_error_string" );

		// Verify we have the functions we want
		if( _REAL_gamemode_request_start && _REAL_gamemode_request_end && _REAL_gamemode_error_string )
		{
			_libgamemode_loaded = 0;
			return 0;
		}
		else
			snprintf( _client_error_string, sizeof(_client_error_string), "dlsym failed - %s", dlerror() );
	}

	_libgamemode_loaded = -1;
	return -1;
}

// Redirect to the real libgamemode
__attribute__((always_inline))
inline const char* gamemode_error_string()
{
	// If we fail to load the system gamemode, return our error string
	if( _load_libgamemode() < 0 )
		return _client_error_string;

	return _REAL_gamemode_error_string();
}

// Redirect to the real libgamemode
// Allow automatically requesting game mode
// Also prints errors as they happen
#ifdef GAMEMODE_AUTO
__attribute__((constructor))
#else
__attribute__((always_inline))
inline
#endif
int gamemode_request_start()
{
	// Need to load gamemode
	if( _load_libgamemode() < 0 )
	{
#ifdef GAMEMODE_AUTO
		fprintf( stderr, "gamemodeauto: %s\n", gamemode_error_string() );
#endif
		return -1;
	}

	if( _REAL_gamemode_request_start() < 0 )
	{
#ifdef GAMEMODE_AUTO
		fprintf( stderr, "gamemodeauto: %s\n", gamemode_error_string() );
#endif
		return -1;
	}

	return 0;
}

// Redirect to the real libgamemode
#ifdef GAMEMODE_AUTO
__attribute__((destructor))
#else
__attribute__((always_inline))
inline
#endif
int gamemode_request_end()
{
	// Need to load gamemode
	if( _load_libgamemode() < 0 )
	{
#ifdef GAMEMODE_AUTO
		fprintf( stderr, "gamemodeauto: %s\n", gamemode_error_string() );
#endif
		return -1;
	}

	if( _REAL_gamemode_request_end() < 0 )
	{
#ifdef GAMEMODE_AUTO
		fprintf( stderr, "gamemodeauto: %s\n", gamemode_error_string() );
#endif
		return -1;
	}

	return 0;
}

#endif // _CLIENT_GAMEMODE_H_
