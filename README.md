# GameMode
**GameMode** is a daemon/lib combo for Linux that allows games to request a set of optimisations be temporarily applied to the host OS.

The design has a clear-cut abstraction between the host daemon and library (`gamemoded` and `libgamemode`), and the client loaders (`libgamemodeauto` and `gamemode_client.h`) that allows for safe use without worrying about whether the daemon is installed or running. This design also means that while the host library currently relies on `systemd` for exchanging messages with the daemon, it's entirely possible to implement other internals that still work with the same clients.

GameMode was designed primarily as a stop-gap solution to problems with the Intel and AMD CPU powersave or ondemand governors, but is intended to be expanded beyond just CPU governor states, as there are a wealth of automation tasks one might want to apply.

Issues with GameMode should be reported here in the issues section, and not reported to Feral directly.

---
## Building and installing

If your distribution already has GameMode packaged, it is preferable to install it directly from there. There are Solus and AUR packages already available.

GameMode depends on `meson` for building and `systemd` for internal communication. This repo contains a `bootstrap.sh` script to allow for quick install to the user bus, but check `meson_options.txt` for custom settings.

```bash
# Ubuntu
apt install meson libsystemd-dev pkg-config ninja-build
# Arch
pacman -S meson systemd ninja
# Fedora
dnf install meson systemd-devel pkg-config

git clone https://github.com/FeralInteractive/gamemode.git
cd gamemode
./bootstrap.sh
```

---
## Requesting GameMode

### Users
After installing `libgamemodeauto.so` simply preload it into the game:
```bash
LD_PRELOAD=/usr/\$LIB/libgamemodeauto.so ./game
```
Or edit the steam launch options:
```
LD_PRELOAD=$LD_PRELOAD:/usr/\$LIB/libgamemodeauto.so %command%
```

### Developers
You may want to build the request directly into an app. Note that none of these client methods force your users to have the daemon installed or running - they will safely no-op if the host is missing.

#### Explicit requests
```C
#include "gamemode_client.h"

	if( gamemode_request_start() < 0 ) {
		fprintf( stderr, "gamemode request failed: %s\n", gamemode_error_string() );
	}

	/* run game... */

	gamemode_request_end(); // Not required, gamemoded can clean up after game exits
```

#### Implicit requests
Simply use the header, but with `GAMEMODE_AUTO` defined.
```C
#define GAMEMODE_AUTO
#include "gamemode_client.h"
```

Or, distribute `libgamemodeauto.so` and either add `-lgamemodeauto` to your linker arguments, or add it to an LD\_PRELOAD in a launch script.

---
## Components

### Host
#### gamemoded
Runs in the background, activates game mode on request, refcounts and also checks caller PID lifetime.

Accepts `-d` (daemonize) and `-l` (log to syslog).

#### libgamemode
Internal library used to dispatch requests to the daemon.

Note: `libgamemode` should never be linked with directly.

### Client
#### libgamemodeauto
Simple dynamic library that automatically requests game mode when loaded.

Useful to `LD_PRELOAD` into any game as needed.

#### gamemode\_client.h
Single header lib that lets a game request game mode and handle errors.

---
## Configuration

The daemon can currently be configured using a `gamemode.ini` file in `/usr/share/gamemode/`. It will load the file when starting up.

An example of what the file could look like is found in the `example` directory.

The file parsing uses [inih](https://github.com/benhoyt/inih).

---
## Contributions

### Pull Requests
Pull requests must match with the coding style found in the `.clang-format` file, please run this before commiting:
```
clang-format -i $(find . -name '*.[ch]')
```

### Planned Features
* Additional mode-switch plugins
* User configuration for local mode-switch plugins
* Improved client state tracking (PID is unreliable)
* API to query if game mode is active

### Maintained by
Marc Di Luzio (Feral Interactive)

### Contributions by
Ikey Doherty (Solus Project), Minze Zwerver (Ysblokje)

---
## License

Copyright © 2018 Feral Interactive

GameMode is available under the terms of the BSD 3-Clause License (Revised)

