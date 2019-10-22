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

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

/**
 * Value clamping helper, works like MIN/MAX but constraints a value within the range.
 */
#define CLAMP(l, u, value) MAX(MIN(l, u), MIN(MAX(l, u), value))

/**
 * Little helper to safely print into a buffer, returns a pointer into the buffer
 */
#define buffered_snprintf(b, s, ...)                                                               \
	(snprintf(b, sizeof(b), s, __VA_ARGS__) < (ssize_t)sizeof(b) ? b : NULL)

/**
 * Little helper to safely print into a buffer, returns a newly allocated string
 */
#define safe_snprintf(b, s, ...)                                                                   \
	(snprintf(b, sizeof(b), s, __VA_ARGS__) < (ssize_t)sizeof(b) ? strndup(b, sizeof(b)) : NULL)

/**
 * Helper function: Test, if haystack ends with needle.
 */
static inline const char *strtail(const char *haystack, const char *needle)
{
	char *pos = strstr(haystack, needle);
	if (pos && (strlen(pos) == strlen(needle)))
		return pos;
	return NULL;
}

/**
 * Helper function for autoclosing file-descriptors. Does nothing if the argument
 * is NULL or the referenced integer < 0.
 */
inline void cleanup_close(int *fd_ptr)
{
	if (fd_ptr == NULL || *fd_ptr < 0)
		return;

	(void)close(*fd_ptr);
}

/**
 * Helper macro for autoclosing file-descriptors: use by prefixing the variable,
 * like "autoclose_fd int fd = -1;".
 */
#define autoclose_fd __attribute__((cleanup(cleanup_close)))

/**
 * Helper function for auto-freeing dynamically allocated memory. Does nothing
 * if *ptr is NULL (ptr must not be NULL).
 */
inline void cleanup_free(void *ptr)
{
	/* The function is defined to work with 'void *' because
	 * that will make sure it compiles without warning also
	 * for all types; what we are getting passed into is a
	 * pointer to a pointer though, so we need to cast */
	void *target = *(void **)ptr;
	free(target); /* free can deal with NULL */
}

/**
 * Helper macro for auto-freeing dynamically allocated memory: use by
 * prefixing the variable, like "autofree char *data = NULL;".
 */
#define autofree __attribute__((cleanup(cleanup_free)))
