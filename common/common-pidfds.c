/*

Copyright (c) 2017-2019, Feral Interactive
Copyright (c) 2019, Red Hat
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
#define _GNU_SOURCE
#include <build-config.h>

#include "common-helpers.h"
#include "common-pidfds.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if !HAVE_FN_PIDFD_OPEN
#include <sys/syscall.h>

#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#endif

static int pidfd_open(pid_t pid, unsigned int flags)
{
	return (int)syscall(__NR_pidfd_open, pid, flags);
}
#else
#include <sys/pidfd.h>
#endif

/* pidfd functions */
int open_pidfds(pid_t *pids, int *fds, int count)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		int pid = pids[i];
		int fd = pidfd_open(pid, 0);

		if (fd < 0)
			break;

		fds[i] = fd;
	}

	return i;
}

static int parse_pid(const char *str, pid_t *pid)
{
	unsigned long long int v;
	char *end;
	pid_t p;

	errno = 0;
	v = strtoull(str, &end, 0);
	if (end == str)
		return -ENOENT;
	else if (errno != 0)
		return -errno;

	p = (pid_t)v;

	if (p < 1 || (unsigned long long int)p != v)
		return -ERANGE;

	if (pid)
		*pid = p;

	return 0;
}

static int parse_status_field_pid(const char *val, pid_t *pid)
{
	const char *t;

	t = strrchr(val, '\t');
	if (t == NULL)
		return -ENOENT;

	return parse_pid(t, pid);
}

static int pidfd_to_pid(int fdinfo, int pidfd, pid_t *pid)
{
	autofree char *key = NULL;
	autofree char *val = NULL;
	char name[256] = {
		0,
	};
	bool found = false;
	FILE *f = NULL;
	size_t keylen = 0;
	size_t vallen = 0;
	ssize_t n;
	int fd;
	int r = 0;

	*pid = 0;

	buffered_snprintf(name, "%d", pidfd);

	fd = openat(fdinfo, name, O_RDONLY | O_CLOEXEC | O_NOCTTY);

	if (fd != -1)
		f = fdopen(fd, "r");

	if (f == NULL)
		return -errno;

	do {
		n = getdelim(&key, &keylen, ':', f);
		if (n == -1) {
			r = errno;
			break;
		}

		n = getdelim(&val, &vallen, '\n', f);
		if (n == -1) {
			r = errno;
			break;
		}

		// TODO: strstrip (key);

		if (!strncmp(key, "Pid", 3)) {
			r = parse_status_field_pid(val, pid);
			found = r > -1;
		}

	} while (r == 0 && !found);

	fclose(f);

	if (r < 0)
		return r;
	else if (!found)
		return -ENOENT;

	return 0;
}

int pidfds_to_pids(int *fds, pid_t *pids, int count)
{
	int fdinfo = -1;
	int r = 0;
	int i;

	fdinfo = open_fdinfo_dir();
	if (fdinfo == -1)
		return -1;

	for (i = 0; i < count && r == 0; i++)
		r = pidfd_to_pid(fdinfo, fds[i], &pids[i]);

	(void)close(fdinfo);

	if (r != 0)
		errno = -r;

	return i;
}

/* misc directory helpers */
int open_fdinfo_dir(void)
{
	return open("/proc/self/fdinfo", O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
}
