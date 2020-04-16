#!/bin/bash

# Exit on failure
set -e

# Collect scan-build output
ninja scan-build -C builddir | tee /tmp/scan-build-results.txt

# Invert the output - if this string exists it's a fail
! grep -E '[0-9]+ bugs? found.' /tmp/scan-build-results.txt
 