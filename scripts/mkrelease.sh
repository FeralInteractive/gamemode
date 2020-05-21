#!/bin/bash
set -e

# Simple script to construct a redistributable and complete tarball of the
# gamemode tree, including the subprojects, so that it can be trivially
# packaged by distributions banning networking during build.
meson subprojects download
meson subprojects update

NAME="gamemode"
VERSION=$(git describe --tags --dirty)

./scripts/git-archive-all.sh --format tar --prefix ${NAME}-${VERSION}/ --verbose -t HEAD ${NAME}-${VERSION}.tar
xz -9 "${NAME}-${VERSION}.tar"

# Automatically sign the tarball with GPG key of user running this script
gpg --armor --detach-sign "${NAME}-${VERSION}.tar.xz"
gpg --verify "${NAME}-${VERSION}.tar.xz.asc"

sha256sum "${NAME}-${VERSION}.tar.xz" "${NAME}-${VERSION}.tar.xz.asc" > sha256sums.txt
