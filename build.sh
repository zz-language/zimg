#!/bin/sh
# zimg build hook — compile C wrapper + Rust native crate + emit libvips flags.
#
# Outputs to build/ directory:
#   build/zimg_wrapper.o   — compiled C wrapper object file
#   build/libzimg_native.so — compiled Rust native crate (shared library)
#   build/libzimg_native.dylib — macOS variant
#   build/cflags.txt       — pkg-config cflags
#   build/ldflags.txt      — pkg-config libs

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CSRC_DIR="$SCRIPT_DIR/csrc"
NATIVE_DIR="$SCRIPT_DIR/native"

# ── Check dependencies ──────────────────────────────────────────────

if ! command -v pkg-config >/dev/null 2>&1; then
	echo "error: pkg-config not found" >&2
	exit 1
fi

if ! pkg-config --exists vips 2>/dev/null; then
	echo "error: libvips not found via pkg-config" >&2
	exit 1
fi

if ! command -v cargo >/dev/null 2>&1; then
	echo "error: cargo not found" >&2
	echo "hint: install Rust (https://rustup.rs)" >&2
	exit 1
fi

# ── Compile C wrapper ───────────────────────────────────────────────

CFLAGS=$(pkg-config --cflags vips)
LIBS=$(pkg-config --libs vips)

mkdir -p "$BUILD_DIR"

echo "Compiling csrc/zimg_wrapper.c ..."
cc -c "$CSRC_DIR/zimg_wrapper.c" \
	-o "$BUILD_DIR/zimg_wrapper.o" \
	$CFLAGS \
	-Wall -Wextra -Werror -fPIC

echo "Compiled: build/zimg_wrapper.o"

# ── Compile Rust native crate ───────────────────────────────────────

echo "Compiling native Rust crate ..."
cd "$NATIVE_DIR"
cargo build --release 2>&1

# Find the compiled shared library
SO_FILE=$(find "$NATIVE_DIR/target/release" -maxdepth 1 \
	\( -name "libzimg_native.so" -o -name "libzimg_native.dylib" \) |
	head -1)

if [ -n "$SO_FILE" ]; then
	cp "$SO_FILE" "$BUILD_DIR/"
	echo "Compiled: build/$(basename "$SO_FILE")"
else
	echo "warning: shared library not found, static linking only" >&2
fi

# Also copy the static library for AOT linking
A_FILE="$NATIVE_DIR/target/release/libzimg_native.a"
if [ -f "$A_FILE" ]; then
	cp "$A_FILE" "$BUILD_DIR/"
	echo "Compiled: build/libzimg_native.a"
fi

# ── Save flags ──────────────────────────────────────────────────────

echo "$CFLAGS" >"$BUILD_DIR/cflags.txt"
echo "$LIBS" >"$BUILD_DIR/ldflags.txt"

echo ""
echo "Build complete."
echo "  Static: build/libzimg_native.a  (for AOT)"
echo "  Shared: build/libzimg_native.so (for VM dlopen)"
