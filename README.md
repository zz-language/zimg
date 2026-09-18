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

Prereqs: `libvips` (`pkg-config --exists vips`), the `zz` toolchain.

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
plugin.zzi      raw FFI declarations (flat `zimg_*`, == C symbols)
zz.toml         [native] build hook
build.sh        compiles csrc + Rust glue → build/*.o/.a/.so + ldflags
csrc/           flat C wrapper over libvips (single result slot)
native/         Rust cdylib/staticlib: VM registration + C decls
src/zimg.zz     public entry: `zimg.resize/rotate/blur/live_handles`
src/ops.zz      one-shot lifecycle (load → transform → save → close)
src/image.zz    single-owner handle pipeline (the chaining primitives)
```

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
