#!/bin/sh
# packaging/meson-slim.sh — locked slim libvips configure flags.
#
# Single source of truth for every triple (local builds, CI matrix).
# Usage: meson setup "$BUILD" <src> --prefix=/usr [more flags] $(sh packaging/meson-slim.sh)
#
# Cut: JPEG PNG WebP HEIF-AVIF PDF-load GIF-load EXIF orc/SIMD in;
# TIFF EXR OpenSlide FITS MAT JXL J2K SVG RAW Magick LCMS FFTW pangocairo
# introspection C++ examples out. GIF save needs imagequant/quantizr
# (absent everywhere we build) → GIF is load-only, like PDF.
# PNG backend: spng when present, else libpng (no flag change needed).
#
# Prints one -D flag per line for `$(...)` substitution.

# shellcheck disable=SC2086
cat <<'FLAGS'
--default-library=shared
--buildtype=release
-Dintrospection=disabled
-Dexamples=false
-Dcplusplus=false
-Dtiff=disabled
-Dopenexr=disabled
-Dopenslide=disabled
-Dopenslide-module=disabled
-Dcfitsio=disabled
-Dmatio=disabled
-Dnifti=disabled
-Draw=disabled
-Dopenjpeg=disabled
-Dmagick=disabled
-Djpeg-xl=disabled
-Djpeg-xl-module=disabled
-Dpdfium=disabled
-Drsvg=disabled
-Dpangocairo=disabled
-Dfftw=disabled
-Dlcms=disabled
-Dimagequant=disabled
-Dquantizr=disabled
-Dheif-module=disabled
-Dpoppler-module=disabled
-Dorc=enabled
FLAGS
