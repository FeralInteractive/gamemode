
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

#define _GNU_SOURCE

#include <linux/limits.h>
#include <dirent.h>
#include <sched.h>

#include "common-cpu.h"
#include "common-external.h"
#include "common-helpers.h"
#include "common-logging.h"

#include "gamemode.h"
#include "gamemode-config.h"

#include "build-config.h"

static int read_small_file(char *path, char **buf, size_t *buflen)
{
	FILE *f = fopen(path, "r");

	if (!f) {
		LOG_ERROR("Couldn't open file at %s : %s\n", path, strerror(errno));
		return 0;
	}

	ssize_t nread = getline(buf, buflen, f);

	if (nread == -1) {
		LOG_ERROR("Couldn't read file at %s : %s\n", path, strerror(errno));
		fclose(f);
		return 0;
	}

	fclose(f);

	while (nread > 0 && ((*buf)[nread - 1] == '\n' || (*buf)[nread - 1] == '\r'))
		nread--;

	(*buf)[nread] = '\0';

	return 1;
}

static int walk_sysfs(char *cpulist, char **buf, size_t *buflen, GameModeCPUInfo *info)
{
	char path[PATH_MAX];
	unsigned long long max_cache = 0, max_freq = 0;
	long from, to;

	cpu_set_t *freq_cores = CPU_ALLOC(info->num_cpu);

	char *list = cpulist;
	while ((list = parse_cpulist(list, &from, &to))) {
		for (long cpu = from; cpu < to + 1; cpu++) {
			CPU_SET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->online);

			/* check for L3 cache non-uniformity among the cores */
			int ret =
			    snprintf(path, PATH_MAX, "/sys/devices/system/cpu/cpu%ld/cache/index3/size", cpu);

			if (ret > 0 && ret < PATH_MAX) {
				if (read_small_file(path, buf, buflen)) {
					char *endp;
					unsigned long long cache_size = strtoull(*buf, &endp, 10);

					if (*endp == 'K') {
						cache_size *= 1024;
					} else if (*endp == 'M') {
						cache_size *= 1024 * 1024;
					} else if (*endp == 'G') {
						cache_size *= 1024 * 1024 * 1024;
					} else if (*endp != '\0') {
						LOG_MSG("cpu L3 cache size (%s) on core #%ld is silly\n", *buf, cpu);
						cache_size = 0;
					}

					if (cache_size > max_cache) {
						max_cache = cache_size;
						CPU_ZERO_S(CPU_ALLOC_SIZE(info->num_cpu), info->to_keep);
					}

					if (cache_size == max_cache)
						CPU_SET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->to_keep);
				}
			}

			/* check for frequency non-uniformity among the cores */
			ret = snprintf(path,
			               PATH_MAX,
			               "/sys/devices/system/cpu/cpu%ld/cpufreq/cpuinfo_max_freq",
			               cpu);

			if (ret > 0 && ret < PATH_MAX) {
				if (read_small_file(path, buf, buflen)) {
					unsigned long long freq = strtoull(*buf, NULL, 10);
					unsigned long long cutoff = (freq * 5) / 100;

					if (freq > max_freq) {
						if (max_freq < freq - cutoff)
							CPU_ZERO_S(CPU_ALLOC_SIZE(info->num_cpu), freq_cores);

						max_freq = freq;
					}

					if (freq + cutoff >= max_freq)
						CPU_SET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), freq_cores);
				}
			}
		}
	}

	if (CPU_EQUAL_S(CPU_ALLOC_SIZE(info->num_cpu), info->online, info->to_keep) ||
	    CPU_COUNT_S(CPU_ALLOC_SIZE(info->num_cpu), info->to_keep) == 0) {
		LOG_MSG("cpu L3 cache was uniform, this is not a x3D with multiple chiplets\n");

		CPU_FREE(info->to_keep);
		info->to_keep = freq_cores;

		if (CPU_EQUAL_S(CPU_ALLOC_SIZE(info->num_cpu), info->online, info->to_keep) ||
		    CPU_COUNT_S(CPU_ALLOC_SIZE(info->num_cpu), info->to_keep) == 0)
			LOG_MSG("cpu frequency was uniform, this is not a big.LITTLE type of system\n");
	} else {
		CPU_FREE(freq_cores);
	}

	return 1;
}

static int walk_string(char *cpulist, char *config_cpulist, GameModeCPUInfo *info)
{
	long from, to;

	char *list = cpulist;
	while ((list = parse_cpulist(list, &from, &to))) {
		for (long cpu = from; cpu < to + 1; cpu++) {
			CPU_SET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->online);

			if (info->park_or_pin == IS_CPU_PARK)
				CPU_SET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->to_keep);
		}
	}

	list = config_cpulist;
	while ((list = parse_cpulist(list, &from, &to))) {
		for (long cpu = from; cpu < to + 1; cpu++) {
			if (CPU_ISSET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->online)) {
				if (info->park_or_pin == IS_CPU_PARK)
					CPU_CLR_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->to_keep);
				else
					CPU_SET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->to_keep);
			}
		}
	}

	return 1;
}

void game_mode_reconfig_cpu(GameModeConfig *config, GameModeCPUInfo **info)
{
	game_mode_unpark_cpu(*info);
	game_mode_free_cpu(info);
	game_mode_initialise_cpu(config, info);
}

int game_mode_initialise_cpu(GameModeConfig *config, GameModeCPUInfo **info)
{
	/* Verify input, this is programmer error */
	if (!info || *info)
		FATAL_ERROR("Invalid GameModeCPUInfo passed to %s", __func__);

	/* Early out if we have this feature turned off */
	char park_cores[CONFIG_VALUE_MAX];
	char pin_cores[CONFIG_VALUE_MAX];
	config_get_cpu_park_cores(config, park_cores);
	config_get_cpu_pin_cores(config, pin_cores);

	int park_or_pin = -1;

	if (pin_cores[0] != '\0') {
		if (strcasecmp(pin_cores, "no") == 0 || strcasecmp(pin_cores, "false") == 0 ||
		    strcmp(pin_cores, "0") == 0) {
			park_or_pin = -2;
		} else if (strcasecmp(pin_cores, "yes") == 0 || strcasecmp(pin_cores, "true") == 0 ||
		           strcmp(pin_cores, "1") == 0) {
			pin_cores[0] = '\0';
			park_or_pin = IS_CPU_PIN;
		} else {
			park_or_pin = IS_CPU_PIN;
		}
	}

	if (park_or_pin != IS_CPU_PIN && park_cores[0] != '\0') {
		if (strcasecmp(park_cores, "no") == 0 || strcasecmp(park_cores, "false") == 0 ||
		    strcmp(park_cores, "0") == 0) {
			if (park_or_pin == -2)
				return 0;

			park_or_pin = -1;
		} else if (strcasecmp(park_cores, "yes") == 0 || strcasecmp(park_cores, "true") == 0 ||
		           strcmp(park_cores, "1") == 0) {
			park_cores[0] = '\0';
			park_or_pin = IS_CPU_PARK;
		} else {
			park_or_pin = IS_CPU_PARK;
		}
	}

	/* always default to pin */
	if (park_or_pin != IS_CPU_PARK)
		park_or_pin = IS_CPU_PIN;

	char *buf = NULL, *buf2 = NULL;
	size_t buflen = 0, buf2len = 0;

	/* first we find which cores are online, this also helps us to determine the max
	 * cpu core number that we need to allocate the cpulist later */
	if (!read_small_file("/sys/devices/system/cpu/online", &buf, &buflen))
		goto error_exit;

	long from, to, max = 0;
	char *s = buf;
	while ((s = parse_cpulist(s, &from, &to))) {
		if (to > max)
			max = to;
	}

	/* either parsing failed or we have only a single core, in either case
	 * we cannot optimize anyway */
	if (max == 0)
		goto early_exit;

	GameModeCPUInfo *new_info = malloc(sizeof(GameModeCPUInfo));
	memset(new_info, 0, sizeof(GameModeCPUInfo));

	new_info->num_cpu = (size_t)(max + 1);
	new_info->park_or_pin = park_or_pin;
	new_info->online = CPU_ALLOC(new_info->num_cpu);
	new_info->to_keep = CPU_ALLOC(new_info->num_cpu);

	CPU_ZERO_S(CPU_ALLOC_SIZE(new_info->num_cpu), new_info->online);
	CPU_ZERO_S(CPU_ALLOC_SIZE(new_info->num_cpu), new_info->to_keep);

	if (park_or_pin == IS_CPU_PARK && park_cores[0] != '\0') {
		if (!walk_string(buf, park_cores, new_info))
			goto error_exit;
	} else if (park_or_pin == IS_CPU_PIN && pin_cores[0] != '\0') {
		if (!walk_string(buf, pin_cores, new_info))
			goto error_exit;
	} else if (!walk_sysfs(buf, &buf2, &buf2len, new_info)) {
		goto error_exit;
	}

	if (park_or_pin == IS_CPU_PARK &&
	    CPU_EQUAL_S(CPU_ALLOC_SIZE(new_info->num_cpu), new_info->online, new_info->to_keep)) {
		game_mode_free_cpu(&new_info);
		LOG_MSG("I can find no reason to perform core parking on this system!\n");
		goto error_exit;
	}

	if (CPU_COUNT_S(CPU_ALLOC_SIZE(new_info->num_cpu), new_info->to_keep) == 0) {
		game_mode_free_cpu(&new_info);
		LOG_MSG("I can find no reason to perform core pinning on this system!\n");
		goto error_exit;
	}

	if (CPU_COUNT_S(CPU_ALLOC_SIZE(new_info->num_cpu), new_info->to_keep) < 4) {
		game_mode_free_cpu(&new_info);
		LOG_MSG(
		    "logic or config would result in less than 4 active cores, will not apply cpu core "
		    "parking/pinning!\n");
		goto error_exit;
	}

	*info = new_info;

early_exit:
	free(buf);
	free(buf2);
	return 0;

error_exit:
	free(buf);
	free(buf2);
	return -1;
}

static int log_state(char *cpulist, int *pos, const long first, const long last)
{
	int ret;
	if (*pos != 0) {
		ret = snprintf(cpulist + *pos, ARG_MAX - (size_t)*pos, ",");

		if (ret < 0 || (size_t)ret >= (ARG_MAX - (size_t)*pos)) {
			LOG_ERROR("snprintf failed, will not apply cpu core parking!\n");
			return 0;
		}

		*pos += ret;
	}

	if (first == last)
		ret = snprintf(cpulist + *pos, ARG_MAX - (size_t)*pos, "%ld", first);
	else
		ret = snprintf(cpulist + *pos, ARG_MAX - (size_t)*pos, "%ld-%ld", first, last);

	if (ret < 0 || (size_t)ret >= (ARG_MAX - (size_t)*pos)) {
		LOG_ERROR("snprintf failed, will not apply cpu core parking!\n");
		return 0;
	}

	*pos += ret;
	return 1;
}

int game_mode_park_cpu(const GameModeCPUInfo *info)
{
	if (!info || info->park_or_pin == IS_CPU_PIN)
		return 0;

	long first = -1, last = -1;

	char cpulist[ARG_MAX];
	int pos = 0;

	for (long cpu = 0; cpu < (long)(info->num_cpu); cpu++) {
		if (CPU_ISSET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->online) &&
		    !CPU_ISSET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->to_keep)) {
			if (first == -1) {
				first = cpu;
				last = cpu;
			} else if (last + 1 == cpu) {
				last = cpu;
			} else {
				if (!log_state(cpulist, &pos, first, last))
					return 0;

				first = cpu;
				last = cpu;
			}
		}
	}

	if (first != -1)
		log_state(cpulist, &pos, first, last);

	const char *const exec_args[] = {
		"pkexec", LIBEXECDIR "/cpucorectl", "offline", cpulist, NULL,
	};

	LOG_MSG("Requesting parking of cores %s\n", cpulist);
	int ret = run_external_process(exec_args, NULL, -1);
	if (ret != 0) {
		LOG_ERROR("Failed to park cpu cores\n");
		return ret;
	}

	return 0;
}

int game_mode_unpark_cpu(const GameModeCPUInfo *info)
{
	if (!info || info->park_or_pin == IS_CPU_PIN)
		return 0;

	long first = -1, last = -1;

	char cpulist[ARG_MAX];
	int pos = 0;

	for (long cpu = 0; cpu < (long)(info->num_cpu); cpu++) {
		if (CPU_ISSET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->online) &&
		    !CPU_ISSET_S((size_t)cpu, CPU_ALLOC_SIZE(info->num_cpu), info->to_keep)) {
			if (first == -1) {
				first = cpu;
				last = cpu;
			} else if (last + 1 == cpu) {
				last = cpu;
			} else {
				if (!log_state(cpulist, &pos, first, last))
					return 0;

				first = cpu;
				last = cpu;
			}
		}
	}

	if (first != -1)
		log_state(cpulist, &pos, first, last);

	const char *const exec_args[] = {
		"pkexec", LIBEXECDIR "/cpucorectl", "online", cpulist, NULL,
	};

	LOG_MSG("Requesting unparking of cores %s\n", cpulist);
	int ret = run_external_process(exec_args, NULL, -1);
	if (ret != 0) {
		LOG_ERROR("Failed to unpark cpu cores\n");
		return ret;
	}

	return 0;
}

static void apply_affinity_mask(pid_t pid, size_t cpusetsize, const cpu_set_t *mask,
                                const bool be_silent)
{
	char buffer[PATH_MAX];
	char *proc_path = NULL;
	DIR *proc_dir = NULL;

	if (!(proc_path = buffered_snprintf(buffer, "/proc/%d/task", pid))) {
		if (!be_silent) {
			LOG_ERROR("Unable to find executable for PID %d: %s\n", pid, strerror(errno));
		}
		return;
	}

	if (!(proc_dir = opendir(proc_path))) {
		if (!be_silent) {
			LOG_ERROR("Unable to find executable for PID %d: %s\n", pid, strerror(errno));
		}
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(proc_dir))) {
		if (entry->d_name[0] == '.')
			continue;

		int tid = atoi(entry->d_name);

		if (sched_setaffinity(tid, cpusetsize, mask) != 0 && !be_silent)
			LOG_ERROR("Failed to pin thread %d: %s\n", tid, strerror(errno));
	}

	closedir(proc_dir);
}

void game_mode_apply_core_pinning(const GameModeCPUInfo *info, const pid_t client,
                                  const bool be_silent)
{
	if (!info || info->park_or_pin == IS_CPU_PARK)
		return;

	if (!be_silent)
		LOG_MSG("Pinning process...\n");

	apply_affinity_mask(client, CPU_ALLOC_SIZE(info->num_cpu), info->to_keep, be_silent);
}

void game_mode_undo_core_pinning(const GameModeCPUInfo *info, const pid_t client)
{
	if (!info || info->park_or_pin == IS_CPU_PARK)
		return;

	LOG_MSG("Pinning process back to all online cores...\n");
	apply_affinity_mask(client, CPU_ALLOC_SIZE(info->num_cpu), info->online, false);
}

void game_mode_free_cpu(GameModeCPUInfo **info)
{
	if ((*info)) {
		CPU_FREE((*info)->online);
		(*info)->online = NULL;

		CPU_FREE((*info)->to_keep);
		(*info)->to_keep = NULL;

		free(*info);
		*info = NULL;
	}
}
