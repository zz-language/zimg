/*
 * zimg_wrapper.c — C wrapper around libvips for ZZ FFI.
 *
 * See zimg_wrapper.h for API docs and ownership contracts.
 */

#include "zimg_wrapper.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vips/vips.h>

/* ── C-plugin ABI stamp ───────────────────────────────────────────── */
/* Pure-C plugin marker for `zz` direct-dlsym loading (no Rust shim).
 * Must equal `zz_runtime::c_abi::C_ABI_VERSION` (currently 1); a mismatch
 * is a clean load refusal, never UB. Bump only with the marshaling contract. */
const unsigned int ZZ_C_PLUGIN_ABI_VERSION = 1;

/* ── Internal state ───────────────────────────────────────────────── */
/* Concurrency contract (§3): every thread runs an independent pipeline.
 * The result slot, pending flag, and error buffer are thread-local, so
 * two threads can load/transform/fail simultaneously without inter-
 * leaving. The live counter is atomic (exact across threads). libvips
 * itself is thread-safe with one global worker pool shared by all
 * threads — no oversubscription by design (see set_workers). Init runs
 * exactly once via pthread_once; concurrent first-loads serialize
 * through it. */

static _Thread_local char _last_error[2048] = {0};
static _Thread_local void* _last_result = NULL;  /* opaque: actually a VipsImage* */

/* Debug-mode result-slot guard (item 6 convention).
 *
 * The single-slot idiom requires every produced image to be consumed via
 * zimg_get_result() before the next producing call. In debug builds
 * (NDEBUG undefined) an overwrite of an unconsumed slot aborts with a
 * clear message instead of silently leaking the image and returning
 * stale data. Define NDEBUG (or ZIMG_NO_SLOT_CHECK) for production to
 * keep the check compiled out.
 */
#ifndef NDEBUG
#ifndef ZIMG_NO_SLOT_CHECK
static _Thread_local int _result_pending = 0;  /* 1 while slot holds an unconsumed image */

static void _slot_check_overwrite(const char* producer) {
    if (_result_pending && _last_result) {
        fprintf(stderr,
                "zimg FATAL: result slot overwritten before consumption\n"
                "  producer `%s` ran while a previous result was never\n"
                "  retrieved via zimg_get_result(). Retrieve every produced\n"
                "  image exactly once, or define NDEBUG for production.\n",
                producer);
        abort();
    }
}

static void _slot_mark_consumed(void) {
    _result_pending = 0;
}
#define ZIMG_SLOT_GUARD(producer) _slot_check_overwrite(producer)
#define ZIMG_SLOT_SET() (_result_pending = 1)
#define ZIMG_SLOT_CONSUMED() (_slot_mark_consumed())
#else
#define ZIMG_SLOT_GUARD(producer) ((void)0)
#define ZIMG_SLOT_SET() ((void)0)
#define ZIMG_SLOT_CONSUMED() ((void)0)
#endif
#else
#define ZIMG_SLOT_GUARD(producer) ((void)0)
#define ZIMG_SLOT_SET() ((void)0)
#define ZIMG_SLOT_CONSUMED() ((void)0)
#endif

/* ── Error helpers ────────────────────────────────────────────────── */

static void _set_error(const char* msg) {
    if (msg && msg[0]) {
        size_t len = strlen(msg);
        if (len >= sizeof(_last_error)) len = sizeof(_last_error) - 1;
        memcpy(_last_error, msg, len);
        _last_error[len] = '\0';
    } else {
        _last_error[0] = '\0';
    }
}

static void _clear_error(void) { _last_error[0] = '\0'; }

/* ── Slim format surface ──────────────────────────────────────────── */
/* Supported formats: PNG JPG WebP GIF HEIF AVIF PDF (load); PDF is
 * load-only (libvips has no PDF saver). Everything else (TIFF, OpenEXR,
 * FITS, MAT, SVG, …) is rejected here with a clear error instead of a
 * deep libvips failure. Case-insensitive. */

static int _ext_matches(const char* path, const char* const* exts) {
    const char* dot = strrchr(path, '.');
    if (!dot || !dot[1]) return 0;
    for (int i = 0; exts[i]; i++) {
        const char* a = dot + 1;
        const char* b = exts[i];
        for (;;) {
            char ca = *a;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (ca != *b) break;
            if (ca == '\0') return 1;
            a++;
            b++;
        }
    }
    return 0;
}

static const char* const _load_exts[] = {
    "png", "jpg", "jpeg", "webp", "gif", "heif", "heic", "avif", "pdf", NULL
};

static const char* const _save_exts[] = {
    "png", "jpg", "jpeg", "webp", "gif", "heif", "heic", "avif", NULL
};

/* ── Result slot ──────────────────────────────────────────────────── */
/* Stores the VipsImage* produced by the last image-producing call.   */
/* Caller retrieves it via zimg_get_result() and takes ownership.     */

/* Debug live-handle counter: incremented for every produced image,
 * decremented on every release. Tests assert zero at exit (no leaks).
 * Atomic: exact across threads. Always compiled in. */
static atomic_int _live_handles = 0;

int zimg_live_handles(void) { return atomic_load(&_live_handles); }

static void _set_result(VipsImage* vips, const char* producer) {
    ZIMG_SLOT_GUARD(producer);
    /* Release previous result if any (caller didn't pick it up). */
    if (_last_result) {
        g_object_unref(_last_result);
        _last_result = NULL;
        atomic_fetch_sub(&_live_handles, 1);
    }
    _last_result = vips;
    if (vips) {
        ZIMG_SLOT_SET();
        atomic_fetch_add(&_live_handles, 1);
    }
}

/* ── VIPS log filter ──────────────────────────────────────────────── */
/* System libvips probes every module in its plugin dir at init; a missing
 * optional backend (e.g. openslide) prints a VIPS-WARNING on every process
 * start. Pillow-quiet ergonomics: drop "unable to load …" module noise,
 * forward everything else to the default handler. Real failures surface
 * as vips errors (return codes), never as warnings. */
static void _vips_log_filter(
    const gchar* domain, GLogLevelFlags level, const gchar* message, gpointer _data
) {
    (void)domain;
    (void)level;
    (void)_data;
    if (message && strstr(message, "unable to load \"") != NULL) return;
    g_log_default_handler(domain, level, message, _data);
}

static pthread_once_t _init_once = PTHREAD_ONCE_INIT;
static int _init_rc = 0;

static void _do_init(void) {
    /* Install before VIPS_INIT: module probing happens during init. */
    g_log_set_handler("VIPS", G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE, _vips_log_filter, NULL);
    if (VIPS_INIT("zimg")) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        _init_rc = -1;
        return;
    }
    /* Mogrify semantics: every load must re-read its file, even when a
     * previous load of the same path was overwritten moments ago
     * (same-second mtime keeps libvips' operation cache stale). Disable
     * the operation cache for the process; saves stay synchronous and
     * every load observes the current file contents. */
    vips_cache_set_max(0);
}

int zimg_init(void) {
    /* Slot starts empty on every thread by construction (thread-local),
     * so init can orphan nothing. Error buffer is thread-local: a racing
     * thread's failure cannot clobber this thread's message. */
    _last_result = NULL;
    pthread_once(&_init_once, _do_init);
    return _init_rc;
}

void zimg_shutdown(void) {
    /* Release the CALLING thread's pending slot. Call once, after all
     * threads have joined: vips_shutdown() while workers run is UB. */
    if (_last_result) {
        g_object_unref(_last_result);
        _last_result = NULL;
        atomic_fetch_sub(&_live_handles, 1);
    }
    vips_shutdown();
}

void zimg_set_concurrency(int n) { vips_concurrency_set(n); }

/* ── Operations that produce a new image ──────────────────────────── */

int zimg_load(const char* path) {
    _clear_error();
    if (!path || !path[0]) { _set_error("empty path"); return -1; }
    if (!_ext_matches(path, _load_exts)) {
        _set_error("unsupported format (want png/jpg/webp/gif/heif/avif/pdf)");
        return -1;
    }
    VipsImage* vips = vips_image_new_from_file(path, NULL);
    if (!vips) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(vips, "zimg_load");
    return 0;
}

/* Create a solid-color test image (no file I/O needed). */
int zimg_create_test(int width, int height, int r, int g, int b) {
    _clear_error();
    if (width <= 0 || height <= 0) { _set_error("invalid dimensions"); return -1; }

    /* Create a 3-band RGB image filled with the given color.
     * vips_image_new_from_memory(data, size, width, height, bands, format) */
    unsigned char pixel[3] = { (unsigned char)r, (unsigned char)g, (unsigned char)b };

    /* Create a 1x1 pixel image, then zoom to target size */
    VipsImage* tiny = vips_image_new_from_memory(pixel, sizeof(pixel), 1, 1, 3, VIPS_FORMAT_UCHAR);
    if (!tiny) {
        _set_error("failed to create test image");
        return -1;
    }

    VipsImage* out = NULL;
    if (vips_zoom(tiny, &out, width, height, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        g_object_unref(tiny);
        return -1;
    }
    g_object_unref(tiny);

    _set_result(out, "zimg_create_test");
    return 0;
}

/* Save image via raw path pointer (caller passes *const void → char*). */
int zimg_save(void* img, const char* path) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!path || !path[0]) { _set_error("empty path"); return -1; }
    if (!_ext_matches(path, _save_exts)) {
        _set_error("unsupported save format (want png/jpg/webp/gif/heif/avif)");
        return -1;
    }
    if (vips_image_write_to_file((VipsImage*)img, path, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    return 0;
}

int zimg_resize(void* img, double scale) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!(scale > 0.0)) { _set_error("scale must be positive"); return -1; }
    VipsImage* out = NULL;
    if (vips_resize((VipsImage*)img, &out, scale, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_resize");
    return 0;
}

int zimg_blur(void* img, double sigma) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!(sigma >= 0.0)) { _set_error("sigma must be non-negative"); return -1; }
    VipsImage* out = NULL;
    if (vips_gaussblur((VipsImage*)img, &out, sigma, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_blur");
    return 0;
}

int zimg_crop(void* img, int left, int top, int width, int height) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    VipsImage* out = NULL;
    if (vips_crop((VipsImage*)img, &out, left, top, width, height, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_crop");
    return 0;
}

int zimg_rot(void* img, int angle) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    VipsImage* out = NULL;
    if (vips_rot((VipsImage*)img, &out, angle, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_rot");
    return 0;
}

int zimg_flip(void* img, int direction) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    VipsImage* out = NULL;
    if (vips_flip((VipsImage*)img, &out, direction, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_flip");
    return 0;
}

/* ── Round 2: Pillow-parity ops ─────────────────────────────────────── */

int zimg_resize_kernel(void* img, double scale, int kernel) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!(scale > 0.0)) { _set_error("scale must be positive"); return -1; }
    if (kernel < 0 || kernel >= VIPS_KERNEL_LAST) {
        _set_error("unknown resampling kernel");
        return -1;
    }
    VipsImage* out = NULL;
    if (vips_resize((VipsImage*)img, &out, scale, "kernel", (VipsKernel)kernel, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_resize_kernel");
    return 0;
}

int zimg_thumbnail(void* img, int width, int height, int crop) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (width <= 0 || height <= 0) { _set_error("invalid dimensions"); return -1; }
    if (crop < 0 || crop >= VIPS_INTERESTING_LAST) {
        _set_error("unknown crop mode");
        return -1;
    }
    VipsImage* out = NULL;
    if (vips_thumbnail_image((VipsImage*)img, &out, width,
            "height", height, "crop", (VipsInteresting)crop, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_thumbnail");
    return 0;
}

int zimg_rotate_free(void* img, double angle, int r, int g, int b, int has_bg) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    /* Exact right angles take the lossless path (no resampling, no fill);
     * anything else expands the canvas via vips_rotate. (No libm: the
     * rounding below is manual so consumers never need -lm.) */
    double q = angle / 90.0;
    long n = (long)(q >= 0.0 ? q + 0.5 : q - 0.5);
    double dq = q - (double)n;
    if (dq < 0.0) dq = -dq;
    VipsImage* out = NULL;
    if (dq < 1e-9) {
        int quad = (int)(((n % 4) + 4) % 4);
        if (vips_rot((VipsImage*)img, &out, quad, NULL)) {
            _set_error(vips_error_buffer());
            vips_error_clear();
            return -1;
        }
        _set_result(out, "zimg_rotate_free");
        return 0;
    }
    /* Background: transparent when the image carries alpha and the caller
     * gave no colour; otherwise the caller's rgb, defaulting to black. */
    VipsImage* src = (VipsImage*)img;
    int bands = vips_image_get_bands(src);
    int has_alpha = vips_image_hasalpha(src);
    if (bands > 4) bands = 4;
    double bgvals[4];
    if (!has_bg && has_alpha) {
        for (int i = 0; i < bands; i++) bgvals[i] = 0.0;
    } else {
        double rgb[3] = {(double)r, (double)g, (double)b};
        for (int i = 0; i < bands; i++)
            bgvals[i] = (has_alpha && i == bands - 1) ? 255.0 : rgb[i < 3 ? i : 0];
    }
    VipsArrayDouble* bg = vips_array_double_new(bgvals, bands);
    int rc = vips_rotate(src, &out, angle, "background", bg, NULL);
    vips_area_unref(VIPS_AREA(bg));
    if (rc) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_rotate_free");
    return 0;
}

int zimg_grayscale(void* img) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    VipsImage* out = NULL;
    if (vips_colourspace((VipsImage*)img, &out, VIPS_INTERPRETATION_B_W, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_grayscale");
    return 0;
}

int zimg_brightness_contrast(void* img, double brightness, double contrast) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    VipsImage* out = NULL;
    /* out = in * contrast + brightness (brightness in absolute levels). */
    if (vips_linear1((VipsImage*)img, &out, contrast, brightness, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_brightness_contrast");
    return 0;
}

int zimg_to_colorspace(void* img, int cs) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (cs < 0 || cs >= VIPS_INTERPRETATION_LAST) {
        _set_error("unknown colorspace");
        return -1;
    }
    VipsImage* out = NULL;
    if (vips_colourspace((VipsImage*)img, &out, (VipsInterpretation)cs, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_to_colorspace");
    return 0;
}

int zimg_sharpen(void* img, double sigma) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!(sigma >= 0.0)) { _set_error("sigma must be non-negative"); return -1; }
    /* Sharpen routes through Lab internally and rejects untagged input:
     * ensure sRGB first (noop when already sRGB). The temp is unref'd
     * directly — never slot-tracked, never counted. */
    VipsImage* src = (VipsImage*)img;
    VipsImage* casted = NULL;
    if (src->Type != VIPS_INTERPRETATION_sRGB) {
        if (vips_colourspace(src, &casted, VIPS_INTERPRETATION_sRGB, NULL)) {
            _set_error(vips_error_buffer());
            vips_error_clear();
            return -1;
        }
        src = casted;
    }
    VipsImage* out = NULL;
    int rc = vips_sharpen(src, &out, "sigma", sigma, NULL);
    if (casted) g_object_unref(casted);
    if (rc) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    _set_result(out, "zimg_sharpen");
    return 0;
}

int zimg_composite(void* base, void* overlay, int mode, int x, int y) {
    _clear_error();
    if (!base || !overlay) { _set_error("null image"); return -1; }
    if (mode < 0 || mode >= VIPS_BLEND_MODE_LAST) {
        _set_error("unknown blend mode");
        return -1;
    }
    VipsImage* out = NULL;
    if (vips_composite2((VipsImage*)base, (VipsImage*)overlay, &out,
            (VipsBlendMode)mode, "x", x, "y", y, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    /* Composite consumes BOTH inputs: adopt the result, release the pair.
     * On failure nothing is touched — both handles stay valid. */
    _set_result(out, "zimg_composite");
    g_object_unref(base);
    g_object_unref(overlay);
    atomic_fetch_sub(&_live_handles, 2);
    return 0;
}

/* ── Scalar accessors ─────────────────────────────────────────────── */

int zimg_width(void* img) {
    if (!img) return -1;
    return vips_image_get_width((VipsImage*)img);
}

int zimg_height(void* img) {
    if (!img) return -1;
    return vips_image_get_height((VipsImage*)img);
}

/* ── Disk writes ──────────────────────────────────────────────────── */

int zimg_save_jpeg(void* img, const char* path, int quality) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!path || !path[0]) { _set_error("empty path"); return -1; }
    if (quality < 0 || quality > 100) { _set_error("quality must be 0-100"); return -1; }
    if (vips_jpegsave((VipsImage*)img, path, "Q", quality, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    return 0;
}

int zimg_save_png(void* img, const char* path, int compression) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!path || !path[0]) { _set_error("empty path"); return -1; }
    if (compression < 0 || compression > 9) { _set_error("compression must be 0-9"); return -1; }
    if (vips_pngsave((VipsImage*)img, path, "compression", compression, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    return 0;
}

int zimg_save_webp(void* img, const char* path, int quality) {
    _clear_error();
    if (!img) { _set_error("null image"); return -1; }
    if (!path || !path[0]) { _set_error("empty path"); return -1; }
    if (quality < 0 || quality > 100) { _set_error("quality must be 0-100"); return -1; }
    if (vips_webpsave((VipsImage*)img, path, "Q", quality, NULL)) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    return 0;
}

/* ── Lifecycle ────────────────────────────────────────────────────── */

void zimg_release(void* img) {
    if (img) {
        g_object_unref(img);
        atomic_fetch_sub(&_live_handles, 1);
    }
}

/* ── Result accessors ─────────────────────────────────────────────── */

void* zimg_get_result(void) {
    void* r = _last_result;
    _last_result = NULL;  /* transfer ownership to caller */
    ZIMG_SLOT_CONSUMED();
    return r;
}

/* ── Error handling ───────────────────────────────────────────────── */

const char* zimg_last_error(void) { return _last_error; }

void zimg_clear_error(void) { _clear_error(); }
