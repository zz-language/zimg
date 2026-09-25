# zimg — Pillow-style imaging for ZZ, powered by libvips

```rust
import zimg

func main() {
    zimg.resize("photo.png", 320, 240).expect("resize")
    zimg.rotate("photo.png", 90).expect("rotate")
    zimg.blur("photo.png", 2.0).expect("blur")
}
```

**Positioning: libvips-level performance, Pillow-level ergonomics.** Every
pixel operation runs in libvips — zimg adds no image code of its own, so it
cannot be faster than libvips; what it adds is a small, safe-feeling API on
top. Throughput ≈ raw libvips; ergonomics ≈ Pillow one-liners.

## Use

Prereqs: the `zz` toolchain. libvips resolves automatically:

1. **Prebuilt slim** (`vips-slim-v1.0.0` GitHub Release tarballs, SHA-verified,
   cached in `~/.zz/cache/zimg-vips`) — no install step.
2. **System fallback** (`pkg-config --exists vips`, e.g. `apt install libvips-dev`).

Overrides: `ZIMG_VIPS_DIR=<tree>` (local prebuilt tree),
`ZIMG_VIPS_SYSTEM=1` (force system), `ZIMG_RELEASE_BASE=<url>` (mirror).

The package keyword is `zimg` — add it, import it, nothing else:
`zz add zimg`, then `import zimg`. `image` / `ops` are internal package
modules (never imported by consumers); `vips` / `pillow` are never import names.

```bash
zz registry add zimg --path /home/zaid/Projects/zimg   # once per machine
zz add zimg          # resolves via the registry alias (or --path directly)
zz install
zz build src/main.zz   # AOT — or `zz run` for the interpreter; identical results
```

No init, no handles, no cleanup in user code. One-shot ops use
mogrify semantics (save back to the same path) and return
`Result<str, str>` — the output path, or a static error message. idioms:

```rust
out := zimg.resize("a.png", 800, 600) ?? "fallback.png"
zimg.rotate("a.png", 45).expect("bad angle")   // 90/180/270 only
n := match zimg.blur("a.png", 5.0) {
    .ok(p) => p,
    .err(_e) => "blur failed",
}
```

`zimg.live_handles()` reports produced-minus-released handles
process-wide; assert zero in tests.

## Tests

```bash
cd tests/e2e
./run_tests.sh   # install + every check file on VM (zz run) and AOT (zz build)
```

(`ZZ=/path/to/zz` overrides the toolchain; CI builds it from source.)

17 checks in `src/m1_test.zz` (round-trip, metadata, missing-file) and
`src/ops_test.zz` (all 13 one-shots, angle/name rejections, format
saves, watermark incl. missing-mark). Each check asserts
`live_handles() == 0`; the harness requires `ALL_PASS` on both engines.

ASan re-verification (no valgrind on this box): build any check file
with `ZZ_DUMP_C`, relink the dump with the cached `libzz_rt.a` +
`build/*.o|a` + `pkg-config --libs vips` under `-fsanitize=address`.
Result: zero ASan errors; LSan reports ~6MB in 95×64KB blocks — the
zz host arenas (`zz_arena_init(_, 65536)` per function, never freed at
exit; a hello-world binary leaks 2 the same way). Zero leaked
`VipsImage`s per the live-handle gate.

## Known toolchain gaps (upstream zz, not zimg bugs)

- `zz test` now dlopens `build/*.so` (fixed upstream), but the suite
  keeps its `zz run` programs: they assert end-to-end behavior including
  file I/O and exit codes, which `@test` functions don't cover.
- Historical workarounds kept in the suite (`dim_w`/`dim_h` int helpers,
  one comparison per `assert`): the VM frame-slot bug behind them is
  fixed upstream; the helpers stay as style, not necessity.

## Prebuilt slim libvips

`build.sh` fetches SHA-verified slim trees from the
[`vips-slim-v1.0.0` release](https://github.com/zz-language/zimg/releases/tag/vips-slim-v1.0.0)
into `~/.zz/cache/zimg-vips` (system `pkg-config vips` is the fallback;
`ZIMG_VIPS_DIR` / `ZIMG_VIPS_SYSTEM=1` override).

| Triple | Status | Tree | Tarball |
|---|---|---|---|
| `linux-x64` | built, e2e 4/4 green | 4.1 MB | 1.7 MB |
| `linux-arm64` | CI matrix (`slim-vips.yml`) | — | — |
| `darwin-x64` | CI matrix (`slim-vips.yml`) | — | — |
| `darwin-arm64` | CI matrix (`slim-vips.yml`) | — | — |
| `win-x64` | CI matrix (`slim-vips.yml`) | — | — |

Locked Meson flags live in `packaging/meson-slim.sh` (single source of
truth for local + CI builds). CI attaches tarballs + merged `SHA256SUMS`
to the `vips-slim-v<VERSION>` release (`--clobber` on re-runs).

Slim cut (libvips 8.18.6): JPEG / PNG / WebP / HEIF-AVIF / PDF-load /
GIF-load / EXIF in; TIFF, EXR, OpenSlide, FITS, MAT, JXL, J2K, SVG, RAW,
Magick, LCMS, FFTW, pangocairo, introspection out. Deviations in v1:
PNG via libpng (not spng), GIF load-only (needs imagequant/quantizr);
glib/gobject stay system. The wrapper pins `VIPSHOME` to the tree at
runtime — system modules are never probed (two libvips = deadlock).

## Toolchain pin

No Rust involved: the hook is `cc` + `sh` only. The contract with `zz` is
the `// C-ABI: 1` header in `plugin.zzi` plus the `ZZ_C_PLUGIN_ABI_VERSION`
data symbol in `csrc/zimg_wrapper.c` — both must equal the toolchain's
`zz_runtime::c_abi::C_ABI_VERSION` (currently 1); a mismatch is a clean
load refusal. Compatibility check: `cd tests/e2e && ./run_tests.sh` —
green 4/4 means VM (direct dlsym) and AOT agree. Tested against
`zz` branch `feat/c-plugin-abi`, libvips 8.18.6.

## Roadmap

- **Buffer I/O** (blocked on upstream): `new_from_buffer` /
  `write_to_buffer` need `bytes` across the plugin ABI, which today
  only allows int/float/bool/str/void/unit/ptr params and int returns
  (`zz_plugin/src/manifest.rs`: `bytes` maps to `Void` → `NonCType`
  rejection). No base64-over-`str` workaround — it would betray the
  positioning. Unblocks the cloud path from the notes (decode uploads,
  encode responses, no temp files).
- **Custom Meson libvips** (notes §1+§5): gated on LGPL redistribution
  review per THIRD_PARTY_NOTICES.md — out of scope until then.

## Pillow side-by-side

| Pillow | zimg |
|---|---|
| `Image.open("a.png").resize((320, 240)).save("a.png")` | `zimg.resize("a.png", 320, 240).expect("resize")` |
| `img.rotate(90, expand=True); img.save("a.png")` | `zimg.rotate("a.png", 90).expect("rotate")` |
| `img.filter(ImageFilter.GaussianBlur(2)); img.save("a.png")` | `zimg.blur("a.png", 2.0).expect("blur")` |

Differences to know:

- **Overwrite, not values.** Pillow returns image objects; zimg one-shots
  write back to the input path and return it. No image values cross the
  API at all.
- **Aspect preserved on resize.** `resize(path, w, h)` fits inside the
  box (scale = min(w/cur_w, h/cur_h)); Pillow's exact-size mode distorts.
- **Angles restricted.** `rotate` accepts 90/180/270 (lossless); anything
  else is a `Result` error, never undefined behavior.
- **Errors are values.** No exceptions: every op returns
  `Result<_, str>`; handle it with `??`, `.expect`, or `match`.

## Layout

```
plugin.zzi      C-ABI declarations (flat `zimg_*`, == C symbols)
zz.toml         [native] build hook
build.sh        pure C: csrc → build/*.o/.so + ldflags (no cargo)
csrc/           flat C wrapper over libvips (single result slot)
src/zimg.zz     public entry: `zimg.resize/rotate/blur/live_handles`
src/ops.zz      one-shot lifecycle (load → transform → save → close)
src/image.zz    single-owner handle pipeline (the chaining primitives)
```

(Removed `src/ffi.zz` / `src/error.zz` pointer-style extern layer and
`src/util.zz` duplicate counter: unreferenced — the int-handle
`plugin.zzi` layer and `image.live_handles()` are canonical.)

Raw `zimg_*` names are intentionally flat and internal: ZZ resolves
free-function calls on one- and two-part paths only, so a deeper
namespace cannot work for them. The supported surface is `zimg.*`.

## Ownership

`image.zz` documents the full contract; the short version:

- Each transform consumes its input handle (releases it) and returns the
  fresh one. Never reuse a handle after passing it on (move semantics by
  convention — stale ints dangle).
- Failures never touch the result slot: the input handle stays valid,
  and the caller gets `.err(...)`.
- Debug builds abort on slot overwrite-before-consume
  (`ZIMG_SLOT_GUARD`); the live counter catches leaks.
- Overwriting a file drops libvips' operation cache for correctness
  (same-second rewrites would otherwise re-load stale pixels);
  `zimg_init` disables the op cache for mogrify-fresh loads.
