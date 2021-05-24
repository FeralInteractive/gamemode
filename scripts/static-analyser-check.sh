#!/bin/bash

# Ensure we are at the project root
cd "$(dirname $0)"/..

# Collect scan-build output
ninja scan-build -C builddir | tee builddir/meson-logs/scan-build.txt

# Invert the output - if this string exists it's a fail
exit ! grep -E '[0-9]+ bugs? found.' builddir/meson-logs/scan-build.txt
