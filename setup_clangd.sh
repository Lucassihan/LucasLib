#!/bin/bash
# Generates compile_flags.txt for clangd using the local VEX SDK paths.
# Run once after cloning, or again if you update the VEX extension.

set -e

GLOBALSTORE="$HOME/Library/Application Support/Code/User/globalStorage/vexrobotics.vexcode"

SDK_ROOT="$GLOBALSTORE/sdk/cpp"
TOOLCHAIN_ROOT="$GLOBALSTORE/tools/cpp"

# Find the SDK version directory (e.g. V5_20240802_15_00_00)
SDK_VERSION=$(ls "$SDK_ROOT/V5/" 2>/dev/null | head -1)
if [ -z "$SDK_VERSION" ]; then
    echo "ERROR: VEX C++ SDK not found. Make sure the VEX extension is installed and has downloaded the SDK."
    exit 1
fi

VEX="$SDK_ROOT/V5/$SDK_VERSION/vexv5"

if [ ! -d "$VEX/include" ]; then
    echo "ERROR: VEX SDK directory found but looks incomplete: $VEX"
    exit 1
fi

# Find the clang headers version (e.g. 8.0.0) — pick the latest
CLANG_VER=$(ls "$VEX/clang/" 2>/dev/null | sort -V | tail -1)
if [ -z "$CLANG_VER" ]; then
    echo "ERROR: Could not find clang headers in VEX SDK."
    exit 1
fi

cat > compile_flags.txt <<EOF
-target
thumbv7-none-eabi
-fshort-enums
-Wno-unknown-attributes
-std=gnu++20
-fno-rtti
-fno-exceptions
-DVexV5
-D__INT32_TYPE__=long
-D__UINT32_TYPE__=unsigned long
-I$VEX/clang/$CLANG_VER/include
-I$VEX/gcc/include/c++/4.9.3
-I$VEX/gcc/include/c++/4.9.3/arm-none-eabi/armv7-ar/thumb
-I$VEX/gcc/include
-I$VEX/gcc/include/sys
-I$VEX/include
-Iinclude
-Iinclude/movement
-Iinclude/misc
EOF

echo "compile_flags.txt generated successfully."
echo "SDK: $VEX"
echo "Clang headers: $CLANG_VER"
echo ""
echo "Reload VS Code window (Cmd+Shift+P -> 'Reload Window') to pick up changes."
