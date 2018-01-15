# GameMode

A preliminary implementation of a daemon/lib combo to allow games to request a performance mode from the host OS on Linux. It was designed primarily as a stop-gap solution to problems with the Intel and AMD CPU powersave or ondemand governors, but is intended to be expanded beyond just CPU power states as needed.

Currently using `sd-bus` on the user bus internally for messaging

---
## Components

### Host
#### gamemoded
Runs in the background, waits for requests, refcounts and also checks caller PID lifetime.

Accepts `-d` (daemonize) and `-l` (log to syslog)

#### libgamemode
Dynamic library to dispatch requests to the daemon

Note: Behaviour of `gamemoded` may change, so `libgamemode` should never be linked with directly.

#### cpugovctl
Small program used to to control the cpu governor.

Accepts `get` (gets current governor) and `set <GOVERNOR>` (sets the current governor).

### Clients
#### libgamemodeauto
Simple dynamic library that automatically requests game mode when loaded. Minimal dependencies.

Useful to `LD_PRELOAD` into any game as needed.

#### gamemode\_client.h
Very small header only lib that lets a game request game mode and handle errors. Minimal dependencies.

Can also be included with `GAMEMODE_AUTO` defined to behave automatically.

---
## Build and install

### Daemon and host library

#### Dependencies
* meson
* systemd

```bash
# Ubuntu
apt install meson libsystemd-dev
# Arch
pacman -S meson systemd
```

```bash
git clone <git repo>
cd gamemode
meson --prefix=/usr build
cd build
ninja
sudo ninja install
```

---
## Using with any game or program

After installing `libgamemodeauto.so` simple preload it into the program. Examples:
```bash
LD_PRELOAD=/usr/\$LIB/libgamemodeauto.so ./game
```
Or steam launch options
```bash
LD_PRELOAD=/usr/\$LIB/libgamemodeauto.so %command%
```

---
## Building into a game or program

You may want to build in functionality directly into an app, this stops the need for users to have to manually set anything up.

`gamemode_client.h` and `libgamemodeauto` are safe to use regardless of whether libgamemode and gamemoded are installed on the system. They also do not require `systemd`.

### Using directly
```C
#include "gamemode_client.h"

	if( gamemode_request_start() < 0 )
		fprintf( stderr, "gamemode request failed: %s\n", gamemode_error_string() );

	/* run game... */

	gamemode_request_end(); // Not required, gamemoded will clean up after game exists anyway
```

### Using automatically

#### Option 1: Include in code
```C
#define GAMEMODE_AUTO
#include "gamemode_client.h"
```

#### Option 2: Link and distribute
Add `-lgamemodeauto` to linker arguments and distribute `libgamemodeauto.so` with the game

#### Option 3: Distribute and script
Distribute `libgamemodeauto.so` with the game and add to LD\_PRELOAD in a launch script

---
## Pull Requests
Pull requests must match with the coding style found in the `.clang-format` file
```
clang-format -i $(find . -name '*.[ch]')
```

---
## TODO

* Use polkit for cpugovctl (currently simply using chmod +4555)
* Implement some kind of user confuguration to allow for whitelists, extra behaviour, etc.
