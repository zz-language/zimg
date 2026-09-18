/*
 * zimg_wrapper.c — C wrapper around libvips for ZZ FFI.
 *
 * See zimg_wrapper.h for API docs and ownership contracts.
 */

#include "zimg_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vips/vips.h>

/* ── Internal state ───────────────────────────────────────────────── */

static char _last_error[2048] = {0};
static void* _last_result = NULL;  /* opaque: actually a VipsImage* */

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
static int _result_pending = 0;  /* 1 while slot holds an unconsumed image */

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

/* ── Result slot ──────────────────────────────────────────────────── */
/* Stores the VipsImage* produced by the last image-producing call.   */
/* Caller retrieves it via zimg_get_result() and takes ownership.     */

/* Debug live-handle counter: incremented for every produced image,
 * decremented on every release. Tests assert zero at exit (no leaks).
 * Always compiled in (two integer ops); not gated on NDEBUG. */
static int _live_handles = 0;

int zimg_live_handles(void) { return _live_handles; }

static void _set_result(VipsImage* vips, const char* producer) {
    ZIMG_SLOT_GUARD(producer);
    /* Release previous result if any (caller didn't pick it up). */
    if (_last_result) {
        g_object_unref(_last_result);
        _last_result = NULL;
        _live_handles--;
    }
    _last_result = vips;
    if (vips) {
        ZIMG_SLOT_SET();
        _live_handles++;
    }
}

/* ── Init / shutdown ──────────────────────────────────────────────── */

int zimg_init(void) {
    _clear_error();
    _last_result = NULL;
    if (VIPS_INIT("zimg")) {
        _set_error(vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    /* Mogrify semantics: every load must re-read its file, even when a
     * previous load of the same path was overwritten moments ago
     * (same-second mtime keeps libvips' operation cache stale). Disable
     * the operation cache for the process; saves stay synchronous and
     * every load observes the current file contents. */
    vips_cache_set_max(0);
    return 0;
}

void zimg_shutdown(void) {
    if (_last_result) {
        g_object_unref(_last_result);
        _last_result = NULL;
        _live_handles--;
    }
    vips_shutdown();
}

void zimg_set_concurrency(int n) { vips_concurrency_set(n); }

/* ── Operations that produce a new image ──────────────────────────── */

int zimg_load(const char* path) {
    _clear_error();
    if (!path || !path[0]) { _set_error("empty path"); return -1; }
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
        _live_handles--;
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
