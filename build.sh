#!/bin/sh
# zimg build hook — compile C wrapper + emit libvips flags.
#
# Usage:
#   ./build.sh          # compile wrapper + print flags
#   ./build.sh --flags  # print flags only (for manual clang invocation)
#
# Outputs to build/ directory:
#   build/zimg_wrapper.o   — compiled wrapper object file
#   build/cflags.txt       — pkg-config cflags
#   build/ldflags.txt      — pkg-config libs
#
# The ZZ build system does not natively support linking external .o files.
# After `zz build` generates prog.c, compile manually:
#   clang prog.c build/zimg_wrapper.o $(cat build/ldflags.txt) -o output

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CSRC_DIR="$SCRIPT_DIR/csrc"

FLAGS_ONLY=0
if [ "$1" = "--flags" ]; then
	FLAGS_ONLY=1
fi

# ── Check dependencies ──────────────────────────────────────────────

if ! command -v pkg-config >/dev/null 2>&1; then
	echo "error: pkg-config not found" >&2
	echo "hint: install pkg-config (apt install pkg-config / brew install pkg-config)" >&2
	exit 1
fi

if ! pkg-config --exists vips 2>/dev/null; then
	echo "error: libvips not found via pkg-config" >&2
	echo "hint: install libvips-dev (apt install libvips-dev / brew install vips)" >&2
	exit 1
fi

# Check minimum version (8.6 — needed for vips_composite constant args)
required="8.6"
installed=$(pkg-config --modversion vips)
if printf '%s\n%s\n' "$required" "$installed" | sort -V | head -n1 | grep -qv "^$required"; then
	echo "error: libvips $installed is too old, need >= $required" >&2
	exit 1
fi

# ── Emit flags ──────────────────────────────────────────────────────

CFLAGS=$(pkg-config --cflags vips)
LIBS=$(pkg-config --libs vips)

if [ "$FLAGS_ONLY" -eq 1 ]; then
	echo "CFLAGS=$CFLAGS"
	echo "LIBS=$LIBS"
	exit 0
fi

# ── Compile C wrapper ───────────────────────────────────────────────

mkdir -p "$BUILD_DIR"

echo "Compiling csrc/zimg_wrapper.c ..."
cc -c "$CSRC_DIR/zimg_wrapper.c" \
	-o "$BUILD_DIR/zimg_wrapper.o" \
	$CFLAGS \
	-Wall -Wextra -Werror -fPIC

echo "Compiled: build/zimg_wrapper.o"

# ── Save flags for manual link step ─────────────────────────────────

echo "$CFLAGS" >"$BUILD_DIR/cflags.txt"
echo "$LIBS" >"$BUILD_DIR/ldflags.txt"

echo ""
echo "Build complete. To link with a ZZ program:"
echo "  1. zz build          (generates prog.c)"
echo "  2. clang prog.c build/zimg_wrapper.o \$(cat build/ldflags.txt) -o output"
echo ""
echo "Or use the --run flag to do both steps:"
echo "  ./build.sh --run <main.zz>"
