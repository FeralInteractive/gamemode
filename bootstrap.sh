#!/bin/bash
# Simple bootstrap script to build and run the daemon

if [ "$EUID" -eq 0 ]
  then echo "Please don't run bootstrap.sh as root."
  exit
fi

set -e

# Echo the rest so it's obvious
set -x
mkdir -p build
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
systemctl --user start gamemoded
systemctl --user status gamemoded
