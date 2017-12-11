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
#include "gamemode.h"
#include "logging.h"
#include "governors.h"

#include <signal.h>
#include <string.h>

// Storage for game PIDs
#define MAX_GAMES 16
static int game_pids[MAX_GAMES];
static int num_games = 0;

// Constant to control how often we'll wake up and check process ids
static const int wakeup_timer = 5;
static void start_alarm_timer();

// Called once to enter game mode
// Must be followed by a call to leave_game_mode
// Must be called after init_game_mode
static void enter_game_mode()
{
	LOG_MSG( "Entering Game Mode...\n" );
	set_governors( "performance" );

	// Set up the alarm callback
	start_alarm_timer();
}

// Called once to leave game mode
// Must be called after an equivelant call to enter_game_mode
static void leave_game_mode()
{
	LOG_MSG( "Leaving Game Mode...\n" );
	set_governors( NULL );
}

// Set up an alarm callback to check for current process ids
static void alarm_handler( int sig )
{
	// Quick return if no games, and don't register another callback
	if( num_games == 0 )
		return;

	// Check if games are alive at all
	for( int i = 0; i < num_games; )
	{
		int game = game_pids[i];

		if( kill( game, 0 ) != 0 )
		{
			LOG_MSG( "Removing expired game [%i]...\n", game );
			memmove( &game_pids[i], &game_pids[i+1], MAX_GAMES - (i+1) );
			num_games--;
		}
		else
			i++;
	}

	// Either trigger another alarm, or reset the governors
	if( num_games )
		start_alarm_timer();
	else
		leave_game_mode();
}

// Call to trigger starting the alarm timer for pid checks
static void start_alarm_timer()
{
	// Set up process check alarms
	signal( SIGALRM, alarm_handler );
	alarm( wakeup_timer );
}

// Intialise any game mode needs
void init_game_mode()
{
	// Read current governer state before setting up any message handling
	update_initial_gov_state();
	LOG_MSG( "governor is set to [%s]\n", get_initial_governor() );
}

// Called on exit to clean up the governors if appropriate
void term_game_mode()
{
	if( num_games )
		leave_game_mode();
}

// Register a game pid with the game mode
// Will trigger enter game mode if appropriate
void register_game( int pid )
{
	// Check for duplicates
	for( int i = 0; i < num_games; i++ )
	{
		if( game_pids[i] == pid )
		{
			LOG_ERROR( "Addition requested for already known process [%i]\n", pid );
			return;
		}
	}

	// Check we've not already hit max
	if( num_games == MAX_GAMES )
	{
		LOG_ERROR( "Max games (%i) reached, could not add [%i]\n", MAX_GAMES, pid );
		return;
	}

	// Add the game to the database
	LOG_MSG( "Adding game: %i\n", pid );
	game_pids[num_games] = pid;
	num_games++;

	if( num_games == 1 )
		enter_game_mode();
}

// Remove a game from game mode
// Will exit game mode if appropriate
void unregister_game( int pid )
{
	bool found = false;

	// Check list even contains this entry
	for( int i = 0; i < num_games; i++ )
	{
		if( game_pids[i] == pid )
		{
			LOG_MSG( "Removing game: %i\n", pid );
			memmove( &game_pids[i], &game_pids[i+1], MAX_GAMES - (i+1) );
			num_games--;
			found = true;
		}
	}

	if( !found )
	{
		LOG_ERROR( "Removal requested for unknown process [%i]\n", pid );
		return;
	}

	// Leave game mode if needed
	if( num_games == 0 )
		leave_game_mode();
}

