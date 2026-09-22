//! ZZ plugin native crate for zimg.
//!
//! Implements the `zz_plugin_register` entrypoint that the VM loader calls
//! to register native functions. Each function wraps the C wrapper via
//! `extern "C"` declarations.

use std::ffi::CStr;

/// ABI version stamp — must match `CURRENT_ABI_VERSION` in `zz_plugin::loader`.
#[no_mangle]
pub static ZZ_PLUGIN_ABI_VERSION: u32 = 1;

// ---------------------------------------------------------------------------
// C wrapper declarations (from zimg_wrapper.h)
// ---------------------------------------------------------------------------

extern "C" {
    fn zimg_init() -> i32;
    fn zimg_load(path: *const i8) -> i32;
    fn zimg_width(img: *mut std::ffi::c_void) -> i32;
    fn zimg_height(img: *mut std::ffi::c_void) -> i32;
    fn zimg_resize(img: *mut std::ffi::c_void, scale: f64) -> i32;
    fn zimg_blur(img: *mut std::ffi::c_void, sigma: f64) -> i32;
    fn zimg_crop(img: *mut std::ffi::c_void, left: i32, top: i32, width: i32, height: i32) -> i32;
    fn zimg_rot(img: *mut std::ffi::c_void, angle: i32) -> i32;
    fn zimg_flip(img: *mut std::ffi::c_void, direction: i32) -> i32;
    fn zimg_resize_kernel(img: *mut std::ffi::c_void, scale: f64, kernel: i32) -> i32;
    fn zimg_thumbnail(img: *mut std::ffi::c_void, width: i32, height: i32, crop: i32) -> i32;
    fn zimg_rotate_free(
        img: *mut std::ffi::c_void,
        angle: f64,
        r: i32,
        g: i32,
        b: i32,
        has_bg: i32,
    ) -> i32;
    fn zimg_grayscale(img: *mut std::ffi::c_void) -> i32;
    fn zimg_brightness_contrast(img: *mut std::ffi::c_void, brightness: f64, contrast: f64) -> i32;
    fn zimg_to_colorspace(img: *mut std::ffi::c_void, cs: i32) -> i32;
    fn zimg_sharpen(img: *mut std::ffi::c_void, sigma: f64) -> i32;
    fn zimg_composite(
        base: *mut std::ffi::c_void,
        overlay: *mut std::ffi::c_void,
        mode: i32,
        x: i32,
        y: i32,
    ) -> i32;
    fn zimg_create_test(width: i32, height: i32, r: i32, g: i32, b: i32) -> i32;
    fn zimg_save(img: *mut std::ffi::c_void, path: *const i8) -> i32;
    fn zimg_save_jpeg(img: *mut std::ffi::c_void, path: *const i8, quality: i32) -> i32;
    fn zimg_save_png(img: *mut std::ffi::c_void, path: *const i8, compression: i32) -> i32;
    fn zimg_save_webp(img: *mut std::ffi::c_void, path: *const i8, quality: i32) -> i32;
    fn zimg_release(img: *mut std::ffi::c_void);
    fn zimg_get_result() -> *mut std::ffi::c_void;
    fn zimg_live_handles() -> i32;
    fn zimg_set_concurrency(n: i32);
    fn zimg_last_error() -> *const std::ffi::c_void;
}

// ---------------------------------------------------------------------------
// ZZ-native wrappers (receive raw int/float handles, return int/float)
// ---------------------------------------------------------------------------

unsafe fn last_error_msg() -> String {
    let ptr = zimg_last_error();
    if ptr.is_null() {
        return "unknown error".to_string();
    }
    CStr::from_ptr(ptr as *const i8)
        .to_string_lossy()
        .into_owned()
}

/// Helper: extract the `zz_value` as an integer handle.
/// In the VM, `Value::Int(i)` is what `*mut void` maps to for opaque handles.
macro_rules! as_handle {
    ($val:expr) => {{
        match &$val {
            zz_runtime::Value::Int(i) => *i as *mut std::ffi::c_void,
            other => {
                return Err(zz_runtime::EvalError::new(
                    format!("expected int handle, got {other:?}"),
                    zz_runtime::Span::new(0, 0),
                ));
            }
        }
    }};
}

macro_rules! as_float {
    ($val:expr) => {{
        match &$val {
            zz_runtime::Value::Float(f) => *f,
            zz_runtime::Value::Int(i) => *i as f64,
            other => {
                return Err(zz_runtime::EvalError::new(
                    format!("expected float, got {other:?}"),
                    zz_runtime::Span::new(0, 0),
                ));
            }
        }
    }};
}

macro_rules! as_int {
    ($val:expr) => {{
        match &$val {
            zz_runtime::Value::Int(i) => *i,
            other => {
                return Err(zz_runtime::EvalError::new(
                    format!("expected int, got {other:?}"),
                    zz_runtime::Span::new(0, 0),
                ));
            }
        }
    }};
}

macro_rules! ok_int {
    ($val:expr) => {
        Ok(zz_runtime::Value::Int($val as i64))
    };
}

/// Extract a `Value::Str` as an owned Rust string.
macro_rules! as_str {
    ($val:expr) => {{
        match &$val {
            zz_runtime::Value::Str(s) => s.as_str().to_string(),
            other => {
                return Err(zz_runtime::EvalError::new(
                    format!("expected str, got {other:?}"),
                    zz_runtime::Span::new(0, 0),
                ));
            }
        }
    }};
}

/// Copy a Rust `&str` into a NUL-terminated CString for FFI.
fn cstring(s: &str) -> Result<std::ffi::CString, zz_runtime::EvalError> {
    std::ffi::CString::new(s).map_err(|_| {
        zz_runtime::EvalError::new("interior NUL byte in string", zz_runtime::Span::new(0, 0))
    })
}

// ---------------------------------------------------------------------------
// Native function implementations
// ---------------------------------------------------------------------------

fn native_zimg_init(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    args.clear();
    let code = unsafe { zimg_init() };
    ok_int!(code)
}

fn native_zimg_load(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let path = as_str!(args[0]);
    let cpath = cstring(&path)?;
    let code = unsafe { zimg_load(cpath.as_ptr()) };
    ok_int!(code)
}

fn native_zimg_live_handles(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    args.clear();
    ok_int!(unsafe { zimg_live_handles() })
}

fn native_zimg_width(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let w = unsafe { zimg_width(img) };
    ok_int!(w)
}

fn native_zimg_height(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let h = unsafe { zimg_height(img) };
    ok_int!(h)
}

fn native_zimg_resize(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let scale = as_float!(args[1]);
    let code = unsafe { zimg_resize(img, scale) };
    ok_int!(code)
}

fn native_zimg_blur(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let sigma = as_float!(args[1]);
    let code = unsafe { zimg_blur(img, sigma) };
    ok_int!(code)
}

fn native_zimg_crop(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let left = as_int!(args[1]);
    let top = as_int!(args[2]);
    let width = as_int!(args[3]);
    let height = as_int!(args[4]);
    let code = unsafe { zimg_crop(img, left as i32, top as i32, width as i32, height as i32) };
    ok_int!(code)
}

fn native_zimg_rot(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let angle = as_int!(args[1]);
    let code = unsafe { zimg_rot(img, angle as i32) };
    ok_int!(code)
}

fn native_zimg_flip(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let direction = as_int!(args[1]);
    let code = unsafe { zimg_flip(img, direction as i32) };
    ok_int!(code)
}

fn native_zimg_create_test(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let width = as_int!(args[0]) as i32;
    let height = as_int!(args[1]) as i32;
    let r = as_int!(args[2]) as i32;
    let g = as_int!(args[3]) as i32;
    let b = as_int!(args[4]) as i32;
    let code = unsafe { zimg_create_test(width, height, r, g, b) };
    ok_int!(code)
}

fn native_zimg_save(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let path = as_str!(args[1]);
    let cpath = cstring(&path)?;
    let code = unsafe { zimg_save(img, cpath.as_ptr()) };
    ok_int!(code)
}

fn native_zimg_save_jpeg(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let path = as_str!(args[1]);
    let quality = as_int!(args[2]);
    let cpath = cstring(&path)?;
    let code = unsafe { zimg_save_jpeg(img, cpath.as_ptr(), quality as i32) };
    ok_int!(code)
}

fn native_zimg_save_png(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let path = as_str!(args[1]);
    let compression = as_int!(args[2]);
    let cpath = cstring(&path)?;
    let code = unsafe { zimg_save_png(img, cpath.as_ptr(), compression as i32) };
    ok_int!(code)
}

fn native_zimg_save_webp(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let path = as_str!(args[1]);
    let quality = as_int!(args[2]);
    let cpath = cstring(&path)?;
    let code = unsafe { zimg_save_webp(img, cpath.as_ptr(), quality as i32) };
    ok_int!(code)
}

fn native_zimg_release(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    unsafe { zimg_release(img) };
    Ok(zz_runtime::Value::Unit)
}

fn native_zimg_get_result(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    args.clear();
    let ptr = unsafe { zimg_get_result() };
    ok_int!(ptr as isize)
}

fn native_zimg_last_error(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    args.clear();
    let msg = unsafe { last_error_msg() };
    Ok(zz_runtime::Value::Str(Box::new(msg)))
}

// ---------------------------------------------------------------------------
// Plugin registration entrypoint
// ---------------------------------------------------------------------------

#[allow(improper_ctypes_definitions)]
fn native_zimg_resize_kernel(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let scale = as_float!(args[1]);
    let kernel = as_int!(args[2]);
    let code = unsafe { zimg_resize_kernel(img, scale, kernel as i32) };
    ok_int!(code)
}

fn native_zimg_thumbnail(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let width = as_int!(args[1]);
    let height = as_int!(args[2]);
    let crop = as_int!(args[3]);
    let code = unsafe { zimg_thumbnail(img, width as i32, height as i32, crop as i32) };
    ok_int!(code)
}

fn native_zimg_rotate_free(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let angle = as_float!(args[1]);
    let r = as_int!(args[2]);
    let g = as_int!(args[3]);
    let b = as_int!(args[4]);
    let has_bg = as_int!(args[5]);
    let code = unsafe { zimg_rotate_free(img, angle, r as i32, g as i32, b as i32, has_bg as i32) };
    ok_int!(code)
}

fn native_zimg_grayscale(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let code = unsafe { zimg_grayscale(img) };
    ok_int!(code)
}

fn native_zimg_brightness_contrast(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let brightness = as_float!(args[1]);
    let contrast = as_float!(args[2]);
    let code = unsafe { zimg_brightness_contrast(img, brightness, contrast) };
    ok_int!(code)
}

fn native_zimg_to_colorspace(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let cs = as_int!(args[1]);
    let code = unsafe { zimg_to_colorspace(img, cs as i32) };
    ok_int!(code)
}

fn native_zimg_sharpen(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let img = as_handle!(args[0]);
    let sigma = as_float!(args[1]);
    let code = unsafe { zimg_sharpen(img, sigma) };
    ok_int!(code)
}

fn native_zimg_composite(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let base = as_handle!(args[0]);
    let overlay = as_handle!(args[1]);
    let mode = as_int!(args[2]);
    let x = as_int!(args[3]);
    let y = as_int!(args[4]);
    let code = unsafe { zimg_composite(base, overlay, mode as i32, x as i32, y as i32) };
    ok_int!(code)
}

#[allow(improper_ctypes_definitions)] // NativeFn is an extern-C fn pointer; safe here
fn native_zimg_set_concurrency(
    _interp: &mut zz_runtime::eval::Interp,
    args: &mut Vec<zz_runtime::Value>,
    _span: zz_runtime::Span,
) -> Result<zz_runtime::Value, zz_runtime::EvalError> {
    let n = as_int!(args[0]);
    unsafe { zimg_set_concurrency(n as i32) };
    Ok(zz_runtime::Value::Unit)
}

#[allow(improper_ctypes_definitions)] // NativeFn is an extern-C fn pointer; safe here
type RegisterCallback = extern "C" fn(name: *const i8, arity: usize, f: zz_runtime::NativeFn);
#[no_mangle]
#[allow(improper_ctypes_definitions)]
pub extern "C" fn zz_plugin_register(callback: RegisterCallback) {
    use std::ffi::CString;

    // Registered under the raw ZZ-visible names from plugin.zzi
    // (`zimg_*`); the CLI aliases C-symbol keys identically.
    let native = |name: &str, arity: usize, f: zz_runtime::NativeFn| {
        let name = CString::new(name).unwrap();
        callback(name.as_ptr(), arity, f);
    };

    native("zimg_init", 0, native_zimg_init);
    native("zimg_load", 1, native_zimg_load);
    native("zimg_width", 1, native_zimg_width);
    native("zimg_height", 1, native_zimg_height);
    native("zimg_resize", 2, native_zimg_resize);
    native("zimg_blur", 2, native_zimg_blur);
    native("zimg_crop", 5, native_zimg_crop);
    native("zimg_rot", 2, native_zimg_rot);
    native("zimg_flip", 2, native_zimg_flip);
    native("zimg_resize_kernel", 3, native_zimg_resize_kernel);
    native("zimg_thumbnail", 4, native_zimg_thumbnail);
    native("zimg_rotate_free", 6, native_zimg_rotate_free);
    native("zimg_grayscale", 1, native_zimg_grayscale);
    native(
        "zimg_brightness_contrast",
        3,
        native_zimg_brightness_contrast,
    );
    native("zimg_to_colorspace", 2, native_zimg_to_colorspace);
    native("zimg_sharpen", 2, native_zimg_sharpen);
    native("zimg_composite", 5, native_zimg_composite);
    native("zimg_create_test", 5, native_zimg_create_test);
    native("zimg_save", 2, native_zimg_save);
    native("zimg_save_jpeg", 3, native_zimg_save_jpeg);
    native("zimg_save_png", 3, native_zimg_save_png);
    native("zimg_save_webp", 3, native_zimg_save_webp);
    native("zimg_release", 1, native_zimg_release);
    native("zimg_get_result", 0, native_zimg_get_result);
    native("zimg_live_handles", 0, native_zimg_live_handles);
    native("zimg_set_concurrency", 1, native_zimg_set_concurrency);
    native("zimg_last_error", 0, native_zimg_last_error);
}
