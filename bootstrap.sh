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

# accept a prefix value as: prefix=/path ./bootstrap.sh
: ${prefix:=/usr}

# Echo the rest so it's obvious
set -x
meson --prefix=$prefix build -Dwith-systemd-user-unit-dir=/etc/systemd/user
cd build
ninja

# Verify user wants to install
set +x
read -p "Install to $prefix? [Yy] " -r
[[ $REPLY =~ ^[Yy]$ ]]
set -x

sudo ninja install

# Reload systemd configuration so that it picks up the new service.
systemctl --user daemon-reload

# Verify user wants to activate now
set +x
read -p "Activate daemon now? [Yy] " -r
[[ $REPLY =~ ^[Yy]$ ]]
set -x

systemctl --user enable gamemoded.service
systemctl --user start gamemoded.service

