#!/bin/sh
# zimg build hook — pure C: compile wrapper + link VM shared library.
#
# Outputs to build/ directory:
#   build/zimg_wrapper.o   — compiled C wrapper object (AOT link)
#   build/libzimg.so       — shared library (VM dlopen, direct dlsym)
#   build/libzimg.dylib    — macOS VM variant (same objects, dynamiclib)
#   build/cflags.txt       — pkg-config cflags
#   build/ldflags.txt      — pkg-config libs
#
# No cargo, no rustc. libvips resolves via prebuilt per-OS assets when
# present (ZIMG_VIPS_DIR), else system pkg-config (last resort).

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CSRC_DIR="$SCRIPT_DIR/csrc"

# ── Resolve libvips flags ─────────────────────────────────────────────
# Override: ZIMG_VIPS_DIR points at a prebuilt slim tree (include/, lib/).

if [ -n "$ZIMG_VIPS_DIR" ]; then
	# shellcheck disable=SC2086
	CFLAGS="-I$ZIMG_VIPS_DIR/include $(pkg-config --cflags glib-2.0 gobject-2.0 2>/dev/null || true)"
	LIBS="-L$ZIMG_VIPS_DIR/lib -lvips"
else
	if ! command -v pkg-config >/dev/null 2>&1; then
		echo "error: pkg-config not found" >&2
		exit 1
	fi

	if ! pkg-config --exists vips 2>/dev/null; then
		echo "error: libvips not found via pkg-config" >&2
		echo "hint: install libvips (apt: libvips-dev) or set ZIMG_VIPS_DIR" >&2
		exit 1
	fi

	CFLAGS=$(pkg-config --cflags vips)
	LIBS=$(pkg-config --libs vips)
fi

mkdir -p "$BUILD_DIR"

# ── Compile C wrapper ─────────────────────────────────────────────────

echo "Compiling csrc/zimg_wrapper.c ..."
# shellcheck disable=SC2086
cc -c "$CSRC_DIR/zimg_wrapper.c" \
	-o "$BUILD_DIR/zimg_wrapper.o" \
	$CFLAGS \
	-Wall -Wextra -Werror -fPIC

echo "Compiled: build/zimg_wrapper.o"

# ── Link VM shared library ────────────────────────────────────────────
# Direct-dlsym loading needs wrapper symbols exported from a plain C
# shared object (Linux .so, macOS .dylib, Windows .dll).

case "$(uname -s)" in
Darwin)
	# shellcheck disable=SC2086
	cc -dynamiclib -fPIC "$BUILD_DIR/zimg_wrapper.o" \
		-o "$BUILD_DIR/libzimg.dylib" \
		$LIBS
	echo "Compiled: build/libzimg.dylib"
	;;
MINGW* | MSYS* | CYGWIN*)
	# shellcheck disable=SC2086
	cc -shared -fPIC "$BUILD_DIR/zimg_wrapper.o" \
		-o "$BUILD_DIR/libzimg.dll" \
		$LIBS
	echo "Compiled: build/libzimg.dll"
	;;
*)
	# shellcheck disable=SC2086
	cc -shared -fPIC "$BUILD_DIR/zimg_wrapper.o" \
		-o "$BUILD_DIR/libzimg.so" \
		$LIBS
	echo "Compiled: build/libzimg.so"
	;;
esac

# ── Save flags ────────────────────────────────────────────────────────

echo "$CFLAGS" >"$BUILD_DIR/cflags.txt"
echo "$LIBS" >"$BUILD_DIR/ldflags.txt"

echo ""
echo "Build complete (pure C, no Rust)."
echo "  Object: build/zimg_wrapper.o (for AOT)"
echo "  Shared: build/libzimg.so     (for VM dlopen)"
