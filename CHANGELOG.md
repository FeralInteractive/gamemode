## 1.8.2

### Changes

* Fix idle inhibitor closing bus connection too early (#466)
* Fix hybrid CPU core pinning (#455)
* Fix unreadable process maps in gamemodelist (#463)
* Fixed crash if dbus is not accesible (#458)
* Various bugfixes and improvements to documentation

### Contributors

* @notpeelz
* @patatahooligan
* Reilly Brogan @ReillyBrogan
* Alexandru Ionut Tripon @Trial97
* Kostadin @kostadinsh
* Daniel Martinez @Calandracas606

[View the full list of contributors](https://github.com/FeralInteractive/gamemode/graphs/contributors?from=2023-12-13&to=2024-08-19&type=c)

## 1.8.1

### Changes

* Fix polkit parse error (#449)

### Contributors

* @Vam-Jam

[View the full list of contributors](https://github.com/FeralInteractive/gamemode/graphs/contributors?from=2022-07-22&to=2023-12-12&type=c)

## 1.8

### Changes

* Add CPU core pinning and parking capability (#416)
* Allow disabling the Linux kernel split lock mitigation (#446)
* Fix building when pidfd_open is available (Fixes build with glibc 2.36) (#379)
* Unify privileged group configuration between pam, systemd, & polkit (#375)
* Various other bugfixes and improved default configuration

### Contributors

* Henrik Holst @HenrikHolst
* Kira Bruneau @kira-bruneau
* James Le Cuirot @chewi
* Hugo Locurcio @Calinou
* Zoltán Nyikos @nyz93
* @ashuntu
* @szymon-gniado

[View the full list of contributors](https://github.com/FeralInteractive/gamemode/graphs/contributors?from=2022-07-22&to=2023-12-06&type=c)

## 1.7

### Changes

* Added new utility: `gamemodelist` (#346)
* Run executables from `PATH` instead of `/usr/bin` (#323)
* Add a trivial `gamemode.conf` file, which creates the gamemode group (#339)
* Various minor bugfixes and updates to documentation

### Contributors

* Sam Gleske @samrocketman
* Kira Bruneau @kira-bruneau
* Stephan Lachnit @stephanlachnit
* Emil Velikov @evelikov-work

[View the full list of contributors](https://github.com/FeralInteractive/gamemode/graphs/contributors?from=2021-02-19&to=2022-07-21&type=c)

## 1.6.1

### Changes

* Use inih r53
* Packaging changes for Arch
* Minor metainfo improvements

### Contributors

* Stephan Lachnit @stephanlachnit
* Alberto Oporto Ames @otreblan

## 1.6

### Changes

* Created new manpages for `gamemoderun` and the example, now called `gamemode-simulate-game`
* Add ability to change lib directory of `gamemoderun`
* Add option to use `elogind`
* Copy default config file to the correct location
* Allow `LD_PRELOAD` to be overridden in `$GAMEMODERUNEXEC`
* Various minor bugfixes
* Improvements to dependency management

### Contributors

* Stephan Lachnit @stephanlachnit
* Rafał Mikrut @qarmin
* Niels Thykier @nthykier
* Stéphane Gleizes @sgleizes

## 1.5.1

### Changes

Minor changes for Debian and Ubuntu packaging:
* Use the preferred logging system rather than defaulting to syslog.
* Prefer the system installation of inih.

### Contributors

* Sebastien Bacher @seb128
* Stephan Lachnit @stephanlachnit

## 1.5

### Changes

* Introduce a new pidfd based set of D-Bus APIs (#173)
* Dynamically change governor on integrated GPUs for improved performance (#179)
* Various other fixes and improvements.

### Contributors

* Alex Smith @aejsmith
* Christian Kellner @gicmo
* Faith Ekstrand @gfxstrand

## 1.4

### Changes

* Add new D-Bus methods/properties for use by external tools such as the [GameMode GNOME Shell extension](https://github.com/gicmo/gamemode-extension/) (#129, #155, #161).
* Fix I/O priority and niceness optimisations to apply to the whole process rather than just the thread that requests GameMode (#142).
* `gamemoded` will now automatically reload the configuration file when it is changed and update optimisations on current clients (#144).
* Add support for using the client library inside Flatpak by communicating with the daemon via a portal (#146).
* Client library now uses libdbus rather than sd-bus (#147).
* Fix `gamemoderun` to use the correct library path depending on whether the app is 32-bit or 64-bit.
* Support the `GAMEMODERUNEXEC` environment variable to specify an extra wrapper command for games launched with `gamemoderun` (e.g. a hybrid GPU wrapper such as `optirun`) (#159).
* Various other fixes and improvements.

### Contributors

* Christian Kellner @gicmo
* Marc Di Luzio @mdiluz
* Matthias Gerstner @mgerstner
* Minze Zwerver @ysblokje
* Stephan Lachnit @stephanlachnit
* Timo Gurr @tgurr

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
