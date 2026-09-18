/*
 * zimg_wrapper.h — C wrapper around libvips for ZZ FFI.
 *
 * ZZ's extern "C" cannot express:
 *   - Double-pointer out-params (VipsImage **out)
 *   - Non-scalar pointer inner types (*mut *mut void)
 *   - Array-of-pointer params (VipsImage **in)
 *   - Pointer-to-integer comparison (ptr == 0 for null check)
 *
 * This wrapper provides a flat, single-pointer API that ZZ can bind.
 * All operations return int status codes (0=ok, -1=error). Operations that
 * produce a new image store the result in a module-static; ZZ retrieves it
 * via zimg_get_result(). ZZ never compares pointers — it only compares ints.
 *
 * Ownership contract:
 *   - Input image handles are BORROWED (refcount unchanged).
 *   - Operations that produce a new image store it internally until
 *     zimg_get_result() is called. After that, the caller owns the handle
 *     and must call zimg_release() when done.
 *   - On failure: status is -1, error via zimg_last_error(). The internal
 *     result is set to NULL — safe to ignore.
 *   - Debug builds (NDEBUG undefined) abort with "result slot overwritten
 *     before consumption" if a producing call runs while a previous
 *     result is still unconsumed. Recommended pattern for all native
 *     libraries using the single-slot idiom. Define NDEBUG (or
 *     ZIMG_NO_SLOT_CHECK) for production.
 *
 * Threading: single-threaded only (global error buffer + result slot).
 */

#ifndef ZIMG_WRAPPER_H
#define ZIMG_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Init / shutdown ──────────────────────────────────────────────── */

/* Call once at program start. Returns 0 on success, -1 on failure. */
int zimg_init(void);

/* Call once at program exit. */
void zimg_shutdown(void);

/* Set libvips thread pool size. 0 = default (CPU cores). */
void zimg_set_concurrency(int n);

/* ── Operations that produce a new image ──────────────────────────── */
/* All return 0 on success, -1 on failure. After success, call       */
/* zimg_get_result() to retrieve the new image handle.               */
/* The input handle remains valid (borrowed, not consumed).          */

/* Load from file path. */
int zimg_load(const char* path);

/* Create a solid-color test image (no file I/O). */
int zimg_create_test(int width, int height, int r, int g, int b);

/* Resize by scale factor (0.5 = half). */
int zimg_resize(void* img, double scale);

/* Gaussian blur. sigma = standard deviation. */
int zimg_blur(void* img, double sigma);

/* Crop to rectangle at (left, top) with given dimensions. */
int zimg_crop(void* img, int left, int top, int width, int height);

/* Rotate by 90-degree increments. 0=0°, 1=90°CW, 2=180°, 3=270°CW. */
int zimg_rot(void* img, int angle);

/* Flip. 0=horizontal, 1=vertical. */
int zimg_flip(void* img, int direction);

/* ── Operations that return a scalar ──────────────────────────────── */

/* Get width in pixels. Returns -1 on error (null handle). */
int zimg_width(void* img);

/* Get height in pixels. Returns -1 on error (null handle). */
int zimg_height(void* img);

/* ── Operations that write to disk ────────────────────────────────── */
/* All return 0 on success, -1 on failure. Input handle is borrowed.  */

/* Save with format auto-detected from extension. */
int zimg_save(void* img, const char* path);

/* Save as JPEG. quality: 0-100. */
int zimg_save_jpeg(void* img, const char* path, int quality);

/* Save as PNG. compression: 0-9. */
int zimg_save_png(void* img, const char* path, int compression);

/* Save as WebP. quality: 0-100. */
int zimg_save_webp(void* img, const char* path, int quality);

/* ── Lifecycle ────────────────────────────────────────────────────── */

/* Release an image handle. Safe to call with NULL (no-op). */
void zimg_release(void* img);

/* Debug live-handle counter: produced images minus releases. Tests
 * assert zero at exit. Always compiled in. */
int zimg_live_handles(void);

/* ── Result accessors ─────────────────────────────────────────────── */

/* Return the result of the last successful operation that produced
 * an image (load, resize, blur, crop, rot, flip). Returns NULL if
 * the last operation failed. Valid until the next image-producing call. */
void* zimg_get_result(void);

/* ── Error handling ───────────────────────────────────────────────── */

/* Last error message. NOT thread-safe. Valid until next zimg_* call. */
const char* zimg_last_error(void);

/* Clear error buffer. */
void zimg_clear_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ZIMG_WRAPPER_H */
