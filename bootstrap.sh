#!/bin/bash
# Simple bootstrap script to build and run the daemon

if [ "$EUID" -eq 0 ]
  then echo "Please don't run bootstrap.sh as root."
  exit
fi

set -e

# Check for scaling governor support and warn about it
if [ ! -f "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" ]; then
	echo "WARNING: CPUFreq scaling governor device file was not found at \"/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor\"."
	echo "This probably means that you have disabled processor scheduling features in your BIOS. See README.md (or GitHub issue #44) for more information."
	echo "This means GameMode's CPU governor control feature will not work (other features will still work)."

	# Allow to continue the install, as gamemode has other useful features
	read -p "Would you like to continue anyway [Y/N]? " -r
	[[ $REPLY =~ ^[Yy]$ ]]
fi

# Echo the rest so it's obvious
set -x
meson --prefix=/usr build -Dwith-systemd-user-unit-dir=/etc/systemd/user
cd build
ninja

# Verify user wants to install
set +x
read -p "Install to /usr? [Yy] " -r
[[ $REPLY =~ ^[Yy]$ ]]
set -x

sudo ninja install

# Verify user wants to run the daemon
set +x
read -p "Enable and run the daemon? [Yy] " -r
[[ $REPLY =~ ^[Yy]$ ]]
set -x

systemctl --user daemon-reload
systemctl --user enable gamemoded
systemctl --user restart gamemoded
systemctl --user status gamemoded
