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
#include "logging.h"

#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>

static const int max_governors       = 128;
static const int max_governor_length = PATH_MAX+1;

// Governers are located at /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
static int fetch_governors( char governors[max_governors][max_governor_length] )
{
	const char* cpu_base_path = "/sys/devices/system/cpu/";
	DIR* dir = opendir( cpu_base_path );
	if( !dir )
		FATAL_ERRORNO( "cpu device path not found" );

	int num_governors = 0;

	// Explore the directory
	struct dirent* ent;
	while( ( ent = readdir(dir) ) && num_governors < max_governors )
	{
		// CPU directories all start with "cpu"
		if( strncmp( ent->d_name, "cpu", 3 ) == 0 )
		{
			// Check if this matches "cpu\d+"
			const int len = strlen( ent->d_name );
			if( len > 3 && len < 5
				&& isdigit( ent->d_name[3] ) )
			{
				// Construct the full path
				char path[PATH_MAX] = {};
				snprintf( path, sizeof(path), "%s%s/cpufreq/scaling_governor", cpu_base_path, ent->d_name );

				// Get the real path to the file
				// Traditionally cpufreq symlinks to a policy directory that can be shared
				// So let's prevent duplicates
				char fullpath[PATH_MAX] = {};
				const char* ptr = realpath( path, fullpath );
				if( fullpath != ptr )
					continue;

				// Only add if unique
				for( int i = 0; i < num_governors; i++ )
				{
					if( strncmp( fullpath, governors[i], max_governor_length ) == 0 )
						continue;
				}

				strncpy( governors[num_governors], fullpath, max_governor_length );
				num_governors++;
			}
		}
	}
	closedir(dir);

	return num_governors;
}

// Get the current governor state
const char* get_gov_state()
{
	// To be returned
	static char governor[64];
	memset( governor, 0, sizeof(governor) );

	// State of all the overnors
	char governors[max_governors][max_governor_length];
	memset( governors, 0, sizeof(governors) );
	int num = fetch_governors( governors );

	// Check the list
	for( int i = 0; i < num; i++ )
	{
		const char* gov = governors[i];

		FILE* f = fopen( gov, "r" );
		if( !f )
		{
			LOG_ERROR( "Failed to open file for read %s\n", gov );
			continue;
		}

		// Pull out the file contents
		fseek( f, 0, SEEK_END );
		int length = ftell(f);
		fseek( f, 0, SEEK_SET );

		char contents[length];

		if( fread(contents, 1, length, f) > 0 )
		{
			// Files have a newline
			strtok(contents, "\n");
			if( strlen(governor) > 0 && strncmp( governor, contents, 64 ) != 0 )
			{
				// Don't handle the mixed case, this shouldn't ever happen
				// But it is a clear sign we shouldn't carry on
				LOG_ERROR( "Governors malformed: got \"%s\", expected \"%s\"", contents, governor );
				return "malformed";
			}
				
			strncpy( governor, contents, sizeof(governor) );
		}
		else
		{
			LOG_ERROR( "Failed to read contents of %s\n", gov );
		}

		fclose( f );
	}

	return governor;
}

// Sets all governors to a value
void set_gov_state( const char* value )
{
	char governors[max_governors][max_governor_length];
	memset( governors, 0, sizeof(governors) );
	int num = fetch_governors( governors );

	LOG_MSG( "Setting governors to %s\n", value );
	for( int i = 0; i < num; i++ )
	{
		const char* gov = governors[i];
		FILE* f = fopen( gov, "w" );
		if( !f )
		{
			LOG_ERROR( "Failed to open file for write %s\n", gov );
			continue;
		}

		fprintf( f, "%s\n", value );
		fclose( f );
	}
}

// Main entry point
int main( int argc, char *argv[] )
{
	if( argc < 2 )
	{
		fprintf( stderr, "usage: cpugovctl [get] [set VALUE]\n" );
		exit( EXIT_FAILURE );
	}

	if( strncmp( argv[1], "get", 3 ) == 0 )
	{
		printf( "%s", get_gov_state() );
	}
	else if( strncmp( argv[1], "set", 3 ) == 0 )
	{
		const char* value = argv[2];
		set_gov_state( value );
	}
	else
	{
		exit( EXIT_FAILURE );
	}
}

