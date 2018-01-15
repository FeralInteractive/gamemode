#!/bin/bash

# Allow cpugovctl to control the governors
PREFIX=${MESON_INSTALL_PREFIX:-/usr}
chmod +4555 ${DESTDIR}${PREFIX}/bin/cpugovctl
