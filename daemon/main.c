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
// Simple daemon to allow user space programs to control the CPU governors
#include "daemonize.h"
#include "dbus_messaging.h"
#include "gamemode.h"
#include "logging.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>

static void sigint_handler(int signo)
{
	LOG_MSG("Quitting by request...\n");

	// Terminate the game mode
	term_game_mode();

	exit(EXIT_SUCCESS);
}

// Main entry point
int main(int argc, char *argv[])
{
	// Gather command line options
	bool daemon = false;
	bool system_dbus = false;
	bool use_syslog = false;
	int opt = 0;
	while ((opt = getopt(argc, argv, "dsl")) != -1) {
		switch (opt) {
		case 'd':
			daemon = true;
			break;
		case 's':
			system_dbus = true;
			break;
		case 'l':
			use_syslog = true;
			break;
		default:
			fprintf(stderr, "Usage: %s [-d] [-s] [-l]\n", argv[0]);
			exit(EXIT_FAILURE);
			break;
		}
	}

	// Use syslog if requested
	if (use_syslog) {
		set_use_syslog(argv[0]);
	}

	// Daemonize ourselves first if asked
	if (daemon) {
		daemonize(argv[0]);
	}

	// Set up the game mode
	init_game_mode();

	// Set up the SIGINT handler
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		FATAL_ERRORNO("Could not catch SIGINT");
	}

	// Run the main dbus message loop
	run_dbus_main_loop(system_dbus);

	// Log we're finished
	LOG_MSG("Quitting naturally...\n");
}
