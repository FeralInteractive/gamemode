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
#ifndef _LOGGING_GAMEMODE_H_
#define _LOGGING_GAMEMODE_H_

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdbool.h>

// Logging helpers
#define PLOG_MSG( msg, ... ) printf( msg, ##__VA_ARGS__ )
#define SYSLOG_MSG( msg, ... ) syslog( LOG_INFO, msg, ##__VA_ARGS__ )
#define LOG_MSG( msg, ... ) do { if( get_use_syslog() ) SYSLOG_MSG( msg, ##__VA_ARGS__ ); else PLOG_MSG( msg, ##__VA_ARGS__ );  } while(0)

#define PLOG_ERROR( msg, ... ) fprintf( stderr, msg, ##__VA_ARGS__ )
#define SYSLOG_ERROR( msg, ... ) syslog( LOG_ERR, msg, ##__VA_ARGS__ )
#define LOG_ERROR( msg, ... ) do { if( get_use_syslog() ) SYSLOG_MSG( msg, ##__VA_ARGS__ ); else PLOG_MSG( msg, ##__VA_ARGS__ );  } while(0)

// Fatal errors trigger an exit
#define FATAL_ERRORNO( msg, ... ) do { LOG_ERROR( msg " (%s)\n", ##__VA_ARGS__, strerror(errno) ); exit(EXIT_FAILURE); } while(0)
#define FATAL_ERROR( msg, ... ) do { LOG_ERROR( msg, ##__VA_ARGS__ ); exit(EXIT_FAILURE); } while(0)

// Control if we want to use the system logger
void set_use_syslog( const char* name );
bool get_use_syslog();

#endif //_LOGGING_GAMEMODE_H_
