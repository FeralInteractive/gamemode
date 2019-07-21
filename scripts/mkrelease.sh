#!/bin/bash
set -e

# Simple script to construct a redistributable and complete tarball of the
# gamemode tree, including the git submodules, so that it can be trivially
# packaged by distributions banning networking during build.
#
# Modified from Ikey Doherty's release scripts for use within
# Feral Interactive's gamemode project.
git submodule init
git submodule update

# Bump in tandem with meson.build, run script once new tag is up.
VERSION="1.5-dev"

NAME="gamemode"
./scripts/git-archive-all.sh --format tar --prefix ${NAME}-${VERSION}/ --verbose -t HEAD ${NAME}-${VERSION}.tar
xz -9 "${NAME}-${VERSION}.tar"

# Automatically sign the tarball with GPG key of user running this script
gpg --armor --detach-sign "${NAME}-${VERSION}.tar.xz"
gpg --verify "${NAME}-${VERSION}.tar.xz.asc"

sha256sum "${NAME}-${VERSION}.tar.xz" "${NAME}-${VERSION}.tar.xz.asc" > sha256sums.txt
