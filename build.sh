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
#
# Link discipline for the cdylib (VM dlopen target):
#   --exclude-libs,ALL: localize symbols from archives (whole-archived
#     rlibs). Only the crate's own objects stay exported, so dead
#     zz_native_rt items (referencing the AOT-only C runtime, e.g.
#     `zz_str_new`) are garbage-collected with --gc-sections instead of
#     dangling at load. The plugin entrypoint + ABI stamp live in the
#     crate's own object and remain exported.
#   -z lazy: Rust links cdylibs -z now; plugins must bind lazily since
#     the `zz` host never provides C runtime symbols.
# Real missing deps (vips, wrapper symbols) still fail: DT_NEEDED libs
# resolve eagerly regardless.

echo "Compiling native Rust crate ..."
cd "$NATIVE_DIR"
# Cargo's fingerprinting routinely misses csrc changes (the C file is an
# input to our build script, but stale fingerprints survive across the
# dual `cargo rustc` / `cargo build` invocations below). If any C source
# is newer than any compiled C object, force the build script to rerun.
STALE_C=0
for obj in "$NATIVE_DIR"/target/release/build/zimg_native-*/out/*.o "$BUILD_DIR/zimg_wrapper.o"; do
	if [ "$CSRC_DIR/zimg_wrapper.c" -nt "$obj" ] || [ "$CSRC_DIR/zimg_wrapper.h" -nt "$obj" ]; then
		STALE_C=1
	fi
done
if [ "$STALE_C" = "1" ]; then
	touch "$NATIVE_DIR/build.rs"
fi
# cdylib (VM dlopen target) with plugin link discipline; scoped to the
# lib target via `cargo rustc` so build scripts are unaffected.
# shellcheck disable=SC2086
cargo rustc --release --lib --crate-type cdylib -- \
	-C link-args=-Wl,--exclude-libs,ALL \
	-C link-args=-Wl,-z,lazy 2>&1
# `cargo rustc --crate-type cdylib` emits into target/release/deps/.
SO_FILE=$(find "$NATIVE_DIR/target/release/deps" -maxdepth 1 -name "libzimg_native.so" -newer "$NATIVE_DIR/Cargo.toml" 2>/dev/null | head -1)
if [ -z "$SO_FILE" ]; then
	SO_FILE="$NATIVE_DIR/target/release/libzimg_native.so"
fi
if [ ! -f "$SO_FILE" ]; then
	SO_FILE="$NATIVE_DIR/target/release/libzimg_native.dylib"
fi
# Copy the flagged cdylib NOW: the plain build below rebuilds it
# without flags (staticlib target) and would overwrite it in target/.
COPIED_SO=""
if [ -f "$SO_FILE" ]; then
	cp "$SO_FILE" "$BUILD_DIR/"
	COPIED_SO="$(basename "$SO_FILE")"
	echo "Compiled: build/$COPIED_SO"
fi
# staticlib (AOT link target): plain build, no special flags.
cargo build --release 2>&1 | tail -1

# Fall back to whatever shared library exists if the flagged copy failed.
if [ -z "$COPIED_SO" ]; then
	SO_FILE=$(find "$NATIVE_DIR/target/release" -maxdepth 1 \
		\( -name "libzimg_native.so" -o -name "libzimg_native.dylib" \) |
		head -1)

	if [ -n "$SO_FILE" ]; then
		cp "$SO_FILE" "$BUILD_DIR/"
		echo "Compiled: build/$(basename "$SO_FILE")"
	else
		echo "warning: shared library not found, static linking only" >&2
	fi
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
