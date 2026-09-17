/*
 * zimg_wrapper.c — C wrapper around libvips for ZZ FFI.
 *
 * See zimg_wrapper.h for API docs and ownership contracts.
 */

#include "zimg_wrapper.h"

#include <stdlib.h>
#include <string.h>
#include <vips/vips.h>

/* ── Internal state ───────────────────────────────────────────────── */

static char _last_error[2048] = {0};
static void* _last_result = NULL;  /* opaque: actually a VipsImage* */

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

static void _set_result(VipsImage* vips) {
    /* Release previous result if any (caller didn't pick it up). */
    if (_last_result) {
        g_object_unref(_last_result);
        _last_result = NULL;
    }
    _last_result = vips;
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
    return 0;
}

void zimg_shutdown(void) {
    if (_last_result) {
        g_object_unref(_last_result);
        _last_result = NULL;
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
    _set_result(vips);
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
    _set_result(out);
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
    _set_result(out);
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
    _set_result(out);
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
    _set_result(out);
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
    _set_result(out);
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
    if (img) g_object_unref(img);
}

/* ── Result accessors ─────────────────────────────────────────────── */

void* zimg_get_result(void) {
    void* r = _last_result;
    _last_result = NULL;  /* transfer ownership to caller */
    return r;
}

/* ── Error handling ───────────────────────────────────────────────── */

const char* zimg_last_error(void) { return _last_error; }

void zimg_clear_error(void) { _clear_error(); }
