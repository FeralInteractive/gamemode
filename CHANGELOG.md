## 1.3.1

### Changes

* Change permission of `gamemoderun` in source tree so that it is correctly installed with execute permissions on older Meson versions (such as that included with Ubuntu 18.04) (#115).
* Enable more compiler warnings and fix issues highlighted by these.

### Contributors

* Christian Kellner @gicmo

## 1.3

### Changes

* Disable screensaver when the game is running (can help when playing with gamepad or joystick).
* Add a `gamemoderun` helper script to do the necessary setup (set `LD_PRELOAD`) to enable GameMode on games which do not support it themselves.
* Support for overclocking on NVIDIA GPUs (experimental, use at your own risk). See `example/gamemode.ini` for further details.
* Support for configuring performance level on AMD GPUs (experimental, use at your own risk). See `example/gamemode.ini`.
* Increase I/O priority of game processes.
* `softrealtime` and `renice` options are no longer enabled by default since they require extra system configuration. See `example/gamemode.ini`.
* Add supervisor API which allows requesting GameMode on behalf of another process.
* Add tests for GameMode functionality (run with `gamemoded -t`).
* Various other minor fixes and improvements.

### Contributors

* Marc Di Luzio @mdiluz
* Kai Krakow @kakra
* Matthias Gerstner @mgerstner
* Suvayu Ali @suvayu
* Térence Clastres @terencode

## 1.2

### Changes

* Store the initial governor state on mode enter.
* Config now supports `defaultgov` and `desiredgov`.
* Add soft real-time scheduling support on kernels supporting SCHED_ISO (`softrealtime` config option) and support for renice-ing games to a higher priority (`renice` config option) (contributed by Kai Krakow).
* Make service D-Bus activated rather than requiring it to be explicitly enabled in systemd (contributed by Christian Kellner).
* Make libraries properly versioned (contributed by Christian Kellner).

## 1.1

### Changes

* Cascaded config file loading.
* `gamemode_query_status` function.
* `-r` (request) and `-s` (status) for gamemoded.
* User defined script plugins in the config file.
* User defined reaper thread frequency.
* Various code refactors and fixes.
* systemd status messages.
* release management scripts.
* inih moved to a git submodule.

## 1.0

### Changes

* Fixed and cleaned up README file.
* Config file parsing.
* Man page.
* Example PKGBUILD file.
* Bug fix for missing `pthread_rwlock_init`.

## 0.2

### Changes

* Updated meson build to improve compatibility, configuration and development.
* cpugovctl now uses polkit.
* Fixed potential threading issues.
* Added option to use the system d-bus for the daemon rather than systemd.
* Various code style and standards related improvements.

## 0.1

Initial release.
