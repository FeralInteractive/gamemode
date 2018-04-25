# GameMode
**GameMode** is a daemon/lib combo for Linux that allows games to request a set of optimisations be temporarily applied to the host OS.

The design has a clear-cut abstraction between the host daemon and library (`gamemoded` and `libgamemode`), and the client loaders (`libgamemodeauto` and `gamemode_client.h`) that allows for safe use without worrying about whether the daemon is installed or running. This design also means that while the host library currently relies on `systemd` for exchanging messages with the daemon, it's entirely possible to implement other internals that still work with the same clients.

GameMode was designed primarily as a stop-gap solution to problems with the Intel and AMD CPU powersave or ondemand governors, but is now able to launch custom user defined plugins, and is intended to be expanded further, as there are a wealth of automation tasks one might want to apply.

Issues with GameMode should be reported here in the issues section, and not reported to Feral directly.

---
## Building and installing

If your distribution already has GameMode packaged, it is preferable to install it directly from there. There are Solus and AUR packages already available.

GameMode depends on `meson` for building and `systemd` for internal communication. This repo contains a `bootstrap.sh` script to allow for quick install to the user bus, but check `meson_options.txt` for custom settings.

```bash
# Ubuntu
apt install meson libsystemd-dev pkg-config ninja-build git
# Arch
pacman -S meson systemd git
# Fedora
dnf install meson systemd-devel pkg-config git

git clone https://github.com/FeralInteractive/gamemode.git
cd gamemode
git checkout 1.1
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
Please note the backslash here in `\$LIB` is required.

### Developers
Developers can build the request directly into an app. Note that none of these client methods force your users to have the daemon installed or running - they will safely no-op if the host is missing.

```C
// Manually with error checking
#include "gamemode_client.h"

	if( gamemode_request_start() < 0 ) {
		fprintf( stderr, "gamemode request failed: %s\n", gamemode_error_string() );
	}

	/* run game... */

	gamemode_request_end(); // Not required, gamemoded can clean up after game exits
```

```C
// Automatically on program start and finish
#define GAMEMODE_AUTO
#include "gamemode_client.h"
```

Or, distribute `libgamemodeauto.so` and either add `-lgamemodeauto` to your linker arguments, or add it to an LD\_PRELOAD in a launch script.

---
## Components

**gamemoded** runs in the background, activates game mode on request, refcounts and also checks caller PID lifetime. Run `man gamemoded` for command line options.

**libgamemode** is an internal library used to dispatch requests to the daemon. Note: `libgamemode` should never be linked with directly.

**libgamemodeauto** is a simple dynamic library that automatically requests game mode when loaded. Useful to `LD_PRELOAD` into any game as needed.

**gamemode\_client.h** is as single header lib that lets a game request game mode and handle errors.

---
## Configuration

The daemon can currently be configured using a `gamemode.ini` file in `/usr/share/gamemode/`. [gamemode.ini](https://github.com/FeralInteractive/gamemode/blob/master/example/gamemode.ini) is an example of what this file would look like, with explanations for all the variables.

The file parsing uses [inih](https://github.com/benhoyt/inih).

---
## Contributions

### Pull Requests
Pull requests must match with the coding style found in the `.clang-format` file, please run this before commiting:
```
clang-format -i $(find . -name '*.[ch]' -not -path "*subprojects/*")
```

### Planned Features
* Additional mode-switch plugins
* Improved client state tracking (PID is unreliable)

### Maintained by
Feral Interactive

See the [contributors](https://github.com/FeralInteractive/gamemode/graphs/contributors) section for an extended list of contributors.

---
## License

Copyright © 2018 Feral Interactive

GameMode is available under the terms of the BSD 3-Clause License (Revised)

