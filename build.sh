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
# Precedence:
#   1. ZIMG_VIPS_DIR — explicit prebuilt slim tree (include/, lib/).
#   2. ZIMG_VIPS_SYSTEM=1 — force system pkg-config (skip prebuilt fetch).
#   3. Prebuilt slim tarball from GitHub Releases
#      (tag vips-slim-v<VERSION>), cached under ~/.zz/cache/zimg-vips.
#      SHA256SUMS-verified; any failure falls through to system.
#   4. System pkg-config (last resort).

ZIMG_VIPS_VERSION="1.0.0"
# Release host (override for mirrors/tests, e.g. file:///tmp/fake-rel).
ZIMG_RELEASE_BASE="${ZIMG_RELEASE_BASE:-https://github.com/zz-language/zimg/releases/download}"

# Host triple slug: linux-x64 | linux-arm64 | darwin-x64 | darwin-arm64 | win-x64.
detect_triple() {
	_os="$(uname -s)"
	_arch="$(uname -m)"
	case "$_os" in
	Linux) _sys="linux" ;;
	Darwin) _sys="darwin" ;;
	MINGW* | MSYS* | CYGWIN*) _sys="win" ;;
	*) _sys="" ;;
	esac
	case "$_arch" in
	x86_64 | amd64) _cpu="x64" ;;
	aarch64 | arm64) _cpu="arm64" ;;
	*) _cpu="" ;;
	esac
	if [ -n "$_sys" ] && [ -n "$_cpu" ]; then
		# win ships x64 only for now.
		if [ "$_sys" = "win" ] && [ "$_cpu" != "x64" ]; then
			return 1
		fi
		printf "%s-%s" "$_sys" "$_cpu"
		return 0
	fi
	return 1
}

# Try fetching the pinned prebuilt slim tree into the cache.
# Prints the tree dir on success; fails otherwise (caller falls back).
fetch_prebuilt() {
	_triple="$1"
	_cache="${XDG_CACHE_HOME:-$HOME/.zz/cache}/zimg-vips/$ZIMG_VIPS_VERSION/$_triple"
	[ -d "$_cache/include" ] && [ -d "$_cache/lib" ] && {
		printf "%s" "$_cache"
		return 0
	}
	_tag="vips-slim-v$ZIMG_VIPS_VERSION"
	_tgz="vips-slim-$_triple.tar.gz"
	_url="$ZIMG_RELEASE_BASE/$_tag/$_tgz"
	_tmp="$(mktemp -d)" || return 1
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL --connect-timeout 8 --max-time 20 -o "$_tmp/$_tgz" "$_url" 2>/dev/null || {
			rm -rf "$_tmp"
			return 1
		}
	elif command -v wget >/dev/null 2>&1; then
		wget -q --timeout=20 -O "$_tmp/$_tgz" "$_url" 2>/dev/null || {
			rm -rf "$_tmp"
			return 1
		}
	else
		rm -rf "$_tmp"
		return 1
	fi
	# Pinned integrity: the release SHA256SUMS must cover the tarball.
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL --connect-timeout 8 --max-time 20 -o "$_tmp/SHA256SUMS" \
			"$ZIMG_RELEASE_BASE/$_tag/SHA256SUMS" 2>/dev/null || {
			rm -rf "$_tmp"
			return 1
		}
	elif command -v wget >/dev/null 2>&1; then
		wget -q --timeout=20 -O "$_tmp/SHA256SUMS" \
			"$ZIMG_RELEASE_BASE/$_tag/SHA256SUMS" 2>/dev/null || {
			rm -rf "$_tmp"
			return 1
		}
	fi
	(cd "$_tmp" && grep -F " $_tgz" SHA256SUMS | sha256sum -c - >/dev/null 2>&1) || {
		rm -rf "$_tmp"
		return 1
	}
	mkdir -p "$_cache" || {
		rm -rf "$_tmp"
		return 1
	}
	tar -xzf "$_tmp/$_tgz" -C "$_cache" 2>/dev/null || {
		rm -rf "$_tmp"
		return 1
	}
	rm -rf "$_tmp"
	[ -d "$_cache/include" ] && [ -d "$_cache/lib" ] && {
		printf "%s" "$_cache"
		return 0
	}
	return 1
}

if [ -n "$ZIMG_VIPS_DIR" ]; then
	# shellcheck disable=SC2086
	CFLAGS="-I$ZIMG_VIPS_DIR/include $(pkg-config --cflags glib-2.0 gobject-2.0 2>/dev/null || true)"
	LIBS="-L$ZIMG_VIPS_DIR/lib -lvips"
elif [ "$ZIMG_VIPS_SYSTEM" != "1" ] && _triple="$(detect_triple)" && _dir="$(fetch_prebuilt "$_triple")"; then
	echo "zimg: using prebuilt slim libvips $ZIMG_VIPS_VERSION ($_triple)" >&2
	case "$(uname -s)" in
	MINGW* | MSYS* | CYGWIN*) _rpath="" ;;
	*) _rpath="-Wl,-rpath,$_dir/lib" ;;
	esac
	# shellcheck disable=SC2086
	CFLAGS="-I$_dir/include $(pkg-config --cflags glib-2.0 gobject-2.0 2>/dev/null || true)"
	LIBS="-L$_dir/lib -lvips $_rpath"
else
	if [ "$ZIMG_VIPS_SYSTEM" != "1" ]; then
		echo "zimg: no prebuilt libvips (offline or unpublished) — system fallback" >&2
	fi
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
