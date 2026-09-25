# Third-Party Notices (zimg)

zimg itself is MIT-licensed (see `LICENSE`). It links against the
following third-party library at runtime. This file is a technical
compliance record, **not legal advice** — get real legal review before
any commercial or wide distribution.

## libvips — LGPL-2.1-or-later

- What: the image-processing engine behind every `zimg.*` operation.
- Version tested: 8.18.6 (via `pkg-config --modversion vips`).
- License: GNU Lesser General Public License v2.1 or later.
- Source: https://github.com/libvips/libvips

### How zimg consumes it (kept LGPL-compliant by construction)

- **Dynamic linkage only.** The C wrapper compiles against libvips
  headers and links `-lvips` (shared). Verified:
  - `ldd build/libzimg.so` → `libvips.so.42` (DT_NEEDED)
  - consumer AOT binary → `libvips.so.42`, with 22 undefined `vips_*`
    references and **zero** defined ones (`nm -D`: `0 T vips_*`)
- No libvips headers are copied into this repo; no libvips source is
  vendored. End users must have a compatible libvips installed
  (system package or equivalent) for zimg binaries to run.

### What this means in practice

- Distributing zimg's own code: MIT terms apply.
- Distributing zimg binaries (or the plugin `.so`/`.a`): the LGPL
  applies to the libvips portion — keep the dynamic link, ship or
  point to the LGPL text and libvips sources as the license requires,
  and don't present libvips as your own code.
- Static linking or vendoring libvips would change this analysis
  entirely — don't do either without re-doing compliance review.
  (Deliberately out of scope for this phase; see README.)
