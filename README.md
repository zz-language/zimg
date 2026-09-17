# zimg

Pillow-level ergonomics at libvips-level performance.

`zimg` wraps [libvips](https://www.libvips.org/) — a demand-driven image processing library that's already faster than Pillow/ImageMagick in most real workloads. The performance ceiling is libvips's own. `zimg`'s value is the ergonomic ZZ API on top.

## Requirements

- **libvips >= 8.6** (dev headers + shared library)
- **pkg-config**
- **clang** (ZZ toolchain backend)

### Install libvips

```bash
# Debian/Ubuntu
sudo apt install libvips-dev pkg-config

# macOS
brew install vips

# Arch
sudo pacman -S vips
```

## Quick Start

```zz
import image

func main() -> Result<int, str> {
    img := image.VipsImage::from_file("photo.jpg")?
    defer img.release()

    resized := img.resize_by_scale(0.5)?
    defer resized.release()

    resized.save("output.jpg")?
    .ok(0)
}
```

## Build

```bash
# Step 1: Compile C wrapper + emit link flags
./build.sh

# Step 2: Generate C from ZZ sources
zz build

# Step 3: Link everything together
clang prog.c build/zimg_wrapper.o $(cat build/ldflags.txt) -o output
```

## API Overview

### Loading

```zz
img := image.VipsImage::from_file("photo.jpg")?
```

### Resizing

```zz
half := img.resize_by_scale(0.5)?      // scale by factor
```

### Saving

```zz
img.save("output.jpg")?               // auto-detect format from extension
```

### Metadata

```zz
w := img.width()                       // pixels
h := img.height()                      // pixels
```

### Cleanup

```zz
defer img.release()                    // must call release, or use defer
```

## Architecture

Four layers:

1. **`csrc/zimg_wrapper.c`** — C wrapper around libvips (handles double-pointer out-params, error reporting)
2. **`src/ffi.zz`** — `extern "C"` bindings to the C wrapper
3. **`src/image.zz`** — Safe `VipsImage` wrapper with lifecycle management
4. **`src/ops.zz`** — Lazy builder API (M2+)

### Why a C Wrapper?

ZZ's `extern "C"` cannot express libvips's double-pointer out-params (`VipsImage **out`), array-of-pointer parameters, or pointer-to-null comparisons. The C wrapper provides a flat, single-pointer API that ZZ can bind, returning int status codes for error checking.

## Memory Safety

- Every `VipsImage` owns exactly one GObject reference
- `defer img.release()` pattern ensures no leaks
- Debug mode: live handle counter asserts zero at process exit
- Valgrind-clean at every milestone gate

## Honest Framing

`zimg` does not outperform libvips — it wraps libvips. The performance characteristics are libvips's own: demand-driven pipeline, low memory footprint, tile-based processing. What `zimg` adds is a ZZ-idiomatic API that's as easy to use as Pillow.

## License

TBD
