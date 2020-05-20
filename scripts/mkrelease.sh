#!/bin/bash
set -e

# Simple script to construct a redistributable and complete tarball of the
# gamemode tree, including the subprojects, so that it can be trivially
# packaged by distributions banning networking during build.

# Bump in tandem with meson.build, run script once new tag is up.
VERSION="1.6-dev"
NAME="gamemode"

# get code in this repo
git archive HEAD --format=tar --prefix=${NAME}-${VERSION}/ --output=${NAME}-${VERSION}.tar
# get code from subprojects
meson subprojects download
meson subprojects update
tar -rf ${NAME}-${VERSION}.tar --exclude-vcs --transform="s,^subprojects,${NAME}-$VERSION/subprojects," subprojects/inih/
# compress archive
xz -9 "${NAME}-${VERSION}.tar"

# Automatically sign the tarball with GPG key of user running this script
gpg --armor --detach-sign "${NAME}-${VERSION}.tar.xz"
gpg --verify "${NAME}-${VERSION}.tar.xz.asc"

sha256sum "${NAME}-${VERSION}.tar.xz" "${NAME}-${VERSION}.tar.xz.asc" > sha256sums.txt
