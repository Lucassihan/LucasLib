#!/bin/sh
# Builds and runs the ramsete math checks with the HOST compiler.
#
# bezier / profile / trajectory / ramseteController include no vex.h, so they
# compile anywhere. The one thing they do need from the library is util.h's
# angle helpers, and the real util.h drags in vex.h — so stubs/ goes first on
# the include path and supplies those few functions instead.
#
# The harness bakes real paths, checks invariants on the rows, and flies a
# simulated unicycle through the actual controller. Nothing here goes near the
# robot build: the VEX makefile only globs src/*/*.cpp.
#
#   sh ramsete-tests/run.sh        (from the LucasLib root)

set -e
cd "$(dirname "$0")/.."
OUT=$(mktemp -d)

clang++ -std=c++17 -O1 -Wall \
    -Iramsete-tests/stubs -Iinclude \
    -o "$OUT/test_ramsete" \
    ramsete-tests/test_ramsete.cpp \
    ramsete-tests/stubs/util.cpp \
    src/ramsete/util/bezierUtils.cpp \
    src/ramsete/util/profile.cpp \
    src/ramsete/util/trajectoryUtils.cpp \
    src/ramsete/util/ramseteController.cpp \
    src/ramsete/util/moveToUtils.cpp \
    src/ramsete/util/boomerangToUtils.cpp

"$OUT/test_ramsete"
