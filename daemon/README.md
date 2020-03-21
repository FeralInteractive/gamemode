### gamemoded
**gamemoded** is a daemon that runs in the background, activates system and program optimisations on request, refcounts and also checks caller lifetime.

**gamemoded** currently supports the current arguments:
```
Usage: gamemoded [-d] [-l] [-r] [-t] [-h] [-v]

  -r[PID], --request=[PID] Toggle gamemode for process
                           When no PID given, requests gamemode and pauses
  -s[PID], --status=[PID]  Query the status of gamemode for process
                           When no PID given, queries the status globally
  -d, --daemonize          Daemonize self after launch
  -l, --log-to-syslog      Log to syslog
  -t, --test               Run tests
  -h, --help               Print this help
  -v, --version            Print version
```

Run `man gamemoded` for information and options.

---
## Daemon Features

### Scheduling
GameMode can leverage support for soft real time mode if the running kernel supports `SCHED_ISO` (not currently supported in upstream kernels), controlled by the `softrealtime` option. This adjusts the scheduling of the game to real time without sacrificing system stability by starving other processes.

GameMode can adjust the nice priority of games to give them a slight IO and CPU priority over other background processes, controlled by the `renice` option. This only works if your user is permitted to adjust priorities within the limits configured by PAM. GameMode can be configured to take care of it by passing `with-pam-group=group` to the build options where `group` is a group your user needs to be part of.
For more information, see `/etc/security/limits.conf`.

Please take note that some games may actually run seemingly slower with `SCHED_ISO` if the game makes use of busy looping while interacting with the graphic driver. The same may happen if you apply too strong nice values. This effect is called priority inversion: Due to the high priority given to busy loops, there may be too few resources left for the graphics driver. Thus, sane defaults were chosen to not expose this effect on most systems. Part of this default is a heuristic which automatically turns off `SCHED_ISO` if GameMode detects three or less CPU cores. Your experience may change based on using GL threaded optimizations, CPU core binding (taskset), the graphic driver, or different CPU architectures. If you experience bad input latency or inconsistent FPS, try switching these configurations on or off first and report back. `SCHED_ISO` comes with a protection against this effect by falling back to normal scheduling as soon as the `SCHED_ISO` process uses more than 70% avarage across all CPU cores. This default value can be adjusted outside of the scope of GameMode (it's in `/proc/sys/kernel/iso_cpu`). This value also protects against compromising system stability, do not set it to 100% as this would turn the game into a full real time process, thus potentially starving all other OS components from CPU resources.

### IO priority
GameMode can adjust the I/O priority of games to benefit from reduced lag and latency when a game has to load assets on demand. This is done by default.

### For those with overclocked CPUs
If you have an AMD CPU and have disabled Cool'n'Quiet, or you have an Intel CPU and have disabled SpeedStep, then GameMode's governor settings will not work, as your CPU is not running with a governor. You are already getting maximum performance.

If you are unsure, `bootstrap.sh` will warn you if your system lacks CPU governor control.

Scripts and other features will still work.

### GPU optimisations
GameMode is able to automatically apply GPU performance mode changes on AMD and NVIDIA, and overclocking on NVIDIA, when activated. AMD support currently requires the `amdgpu` kernel module, and NVIDIA requires the `coolbits` extension to be enabled in the NVIDIA settings.

It is very much encouraged for users to find out their own overclocking limits manually before venturing into configuring them in GameMode, and activating this feature in GameMode assumes you take responsibility for the effects of said overclocks.

More information can be found in the `example/gamemode.ini` file.

Note that both NVIDIA (GPUBoost) and AMD (Overdrive) devices and drivers already attempt to internally overclock if possible, but it is still common for enthusiasts to want to manually push the upper threshold.