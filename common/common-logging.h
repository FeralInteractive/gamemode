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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/* Macros to help with basic logging */
#define PLOG_MSG(...) printf(__VA_ARGS__)
#define SYSLOG_MSG(...) syslog(LOG_INFO, __VA_ARGS__)
#define LOG_MSG(...)                                                                               \
	do {                                                                                           \
		if (get_use_syslog()) {                                                                    \
			SYSLOG_MSG(__VA_ARGS__);                                                               \
		} else {                                                                                   \
			PLOG_MSG(__VA_ARGS__);                                                                 \
		}                                                                                          \
	} while (0)

#define PLOG_ERROR(...) fprintf(stderr, "ERROR: " __VA_ARGS__)
#define SYSLOG_ERROR(...) syslog(LOG_ERR, __VA_ARGS__)
#define LOG_ERROR(...)                                                                             \
	do {                                                                                           \
		if (get_use_syslog()) {                                                                    \
			SYSLOG_ERROR(__VA_ARGS__);                                                             \
		} else {                                                                                   \
			PLOG_ERROR(__VA_ARGS__);                                                               \
		}                                                                                          \
	} while (0)

#define LOG_ONCE(type, ...)                                                                        \
	do {                                                                                           \
		static int __once = 0;                                                                     \
		if (!__once++)                                                                             \
			LOG_##type(__VA_ARGS__);                                                               \
	} while (0)

/* Fatal warnings trigger an exit */
#define FATAL_ERRORNO(msg)                                                                         \
	do {                                                                                           \
		LOG_ERROR(msg " (%s)\n", strerror(errno));                                                 \
		exit(EXIT_FAILURE);                                                                        \
	} while (0)
#define FATAL_ERROR(...)                                                                           \
	do {                                                                                           \
		LOG_ERROR(__VA_ARGS__);                                                                    \
		exit(EXIT_FAILURE);                                                                        \
	} while (0)

/* Hinting helpers */
#define HINT_ONCE(name, hint)                                                                      \
	do {                                                                                           \
		static int __once = 0;                                                                     \
		name = (!__once++ ? hint : "");                                                            \
	} while (0)

#define HINT_ONCE_ON(cond, ...)                                                                    \
	do {                                                                                           \
		if (cond)                                                                                  \
			HINT_ONCE(__VA_ARGS__);                                                                \
	} while (0);

#define LOG_HINTED(type, msg, hint, ...)                                                           \
	do {                                                                                           \
		const char *__arg;                                                                         \
		HINT_ONCE(__arg, hint);                                                                    \
		LOG_##type(msg "%s", __VA_ARGS__, __arg);                                                  \
	} while (0)

/**
 * Control if and how how we use syslog
 */
void set_use_syslog(const char *name);
bool get_use_syslog(void);
