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
 * Threading: thread-safe for independent per-thread pipelines. The
 * result slot, pending flag, and error buffer are thread-local; the
 * live counter is atomic; init runs once via pthread_once. libvips
 * contributes one global worker pool shared by all threads (tune with
 * zimg_set_concurrency). zimg_shutdown() releases the calling thread's
 * slot and shuts libvips down: call once, after all threads join.
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

/* Resize with explicit resampling kernel (VipsKernel int: 0=nearest,
 * 1=linear, 2=cubic, 3=mitchell, 4=lanczos2, 5=lanczos3, ...). */
int zimg_resize_kernel(void* img, double scale, int kernel);

/* Fit inside width×height (thumbnail semantics). crop is VipsInteresting
 * (0=none/fit, 1=centre, ...): nonzero crops to fill exactly. */
int zimg_thumbnail(void* img, int width, int height, int crop);

/* Arbitrary-angle rotate (degrees, clockwise). Exact right angles take
 * the lossless vips_rot path; others expand the canvas via vips_rotate.
 * has_bg=0: transparent when the image has alpha, else black.
 * has_bg=1: rgb fill (opaque alpha when present). */
int zimg_rotate_free(void* img, double angle, int r, int g, int b, int has_bg);

/* Luminance grayscale (B_W interpretation). */
int zimg_grayscale(void* img);

/* Tone: out = in * contrast + brightness (brightness in levels). */
int zimg_brightness_contrast(void* img, double brightness, double contrast);

/* Cast to a VipsInterpretation (int). Errors surface for bad values. */
int zimg_to_colorspace(void* img, int cs);

/* Sharpen (sigma). Auto-casts to sRGB first (noop when already sRGB);
 * sharpen rejects untagged input internally. */
int zimg_sharpen(void* img, double sigma);

/* Pairwise composite (watermark/overlay): overlay placed at (x, y) with
 * blend mode (VipsBlendMode int, 2=over). Consumes BOTH inputs on
 * success (both released, result adopted); touches nothing on failure. */
int zimg_composite(void* base, void* overlay, int mode, int x, int y);

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

/* Last error message. Thread-local: valid until the next zimg_* call
 * on THIS thread; a racing thread's failure cannot clobber it. */
const char* zimg_last_error(void);

/* Clear error buffer. */
void zimg_clear_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ZIMG_WRAPPER_H */
