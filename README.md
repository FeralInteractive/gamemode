# GameMode
**GameMode** is a daemon/lib combo for Linux that allows games to request a set of optimisations be temporarily applied to the host OS.

The design has a clear-cut abstraction between the host daemon and library (`gamemoded` and `libgamemode`), and the client loaders (`libgamemodeauto` and `gamemode_client.h`) that allows for safe use without worrying about whether the daemon is installed or running. This design also means that while the host library currently relies on `systemd` for exchanging messages with the daemon, it's entirely possible to implement other internals that still work with the same clients.

GameMode was designed primarily as a stop-gap solution to problems with the Intel and AMD CPU powersave or ondemand governors, but is now able to launch custom user defined plugins, and is intended to be expanded further, as there are a wealth of automation tasks one might want to apply.

GameMode can leverage support for soft real time mode if the running kernel supports `SCHED_ISO`. This adjusts the scheduling of the game to real time without sacrificing system stability by starving other processes.

GameMode adjusts the nice priority of games to -4 by default to give it a slight IO and CPU priority over other background processes. This only works if your user is permitted to adjust priorities within the limits configured by PAM. See `/etc/security/limits.conf`.

Please take note that some games may actually run seemingly slower with `SCHED_ISO` if the game makes use of busy looping while interacting with the graphic driver. The same may happen if you apply too strong nice values. This effect is called priority inversion: Due to the high priority given to busy loops, there may be too few resources left for the graphics driver. Thus, sane defaults were chosen to not expose this effect on most systems. Part of this default is a heuristic which automatically turns off `SCHED_ISO` if GameMode detects three or less CPU cores. Your experience may change based on using GL threaded optimizations, CPU core binding (taskset), the graphic driver, or different CPU architectures. If you experience bad input latency or inconsistent FPS, try switching these configurations on or off first and report back. `SCHED_ISO` comes with a protection against this effect by falling back to normal scheduling as soon as the `SCHED_ISO` process uses more than 70% avarage across all CPU cores. This default value can be adjusted outside of the scope of GameMode (it's in `/proc/sys/kernel/iso_cpu`). This value also protects against compromising system stability, do not set it to 100% as this would turn the game into a full real time process, thus potentially starving all other OS components from CPU resources.

Issues with GameMode should be reported here in the issues section, and not reported to Feral directly.

---
## Building and installing

If your distribution already has GameMode packaged, it is preferable to install it directly from there. There are Solus and AUR packages already available.

GameMode depends on `meson` for building and `systemd` for internal communication. This repo contains a `bootstrap.sh` script to allow for quick install to the user bus, but check `meson_options.txt` for custom settings.

#### Ubuntu/Debian (you may also need `dbus-user-session`)
```bash
apt install meson libsystemd-dev pkg-config ninja-build git
```
#### Arch
```bash
pacman -S meson systemd git
```
#### Fedora
```bash
dnf install meson systemd-devel pkg-config git
```

Then clone, build and install a release version of GameMode at 1.1:

```bash
git clone https://github.com/FeralInteractive/gamemode.git
cd gamemode
git checkout 1.1
./bootstrap.sh
```

To build the master branch:

```bash
git clone https://github.com/FeralInteractive/gamemode.git
cd gamemode
git submodule update --init --recursive
./bootstrap.sh
```

This will also satisfy the build requirement `inih` by pulling it in as a git submodule.

To uninstall:
```systemctl --user stop gamemoded.service
cd build/
ninja uninstall```

---
## Requesting GameMode

### Users
After installing `libgamemodeauto.so.0` simply preload it into the game:
```bash
LD_PRELOAD=/usr/\$LIB/libgamemodeauto.so.0 ./game
```
Or edit the steam launch options:
```bash
LD_PRELOAD=$LD_PRELOAD:/usr/\$LIB/libgamemodeauto.so.0 %command%
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

The daemon can currently be configured using a `gamemode.ini` file. [gamemode.ini](https://github.com/FeralInteractive/gamemode/blob/master/example/gamemode.ini) is an example of what this file would look like, with explanations for all the variables.

Config files are loaded and merged from the following directories, in order:
1. `/usr/share/gamemode/`
2. `/etc/`
3. `$XDG_CONFIG_HOME` or `$HOME/.config/`
4. `$PWD`

The file parsing uses [inih](https://github.com/benhoyt/inih).

---
## For those with overclocked CPUs
If you have an AMD CPU and have disabled Cool'n'Quiet, or you have an Intel CPU and have disabled SpeedStep, then GameMode's governor settings will not work, as your CPU is not running with a governor. You are already getting maximum performance.

If you are unsure, `bootstrap.sh` will warn you if your system lacks CPU governor control.

Scripts and other features will still work.

---
## Contributions

### Pull Requests
Pull requests must match with the coding style found in the `.clang-format` file, please run this before committing:
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

