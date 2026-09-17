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
    fn zimg_width(img: *mut std::ffi::c_void) -> i32;
    fn zimg_height(img: *mut std::ffi::c_void) -> i32;
    fn zimg_resize(img: *mut std::ffi::c_void, scale: f64) -> i32;
    fn zimg_blur(img: *mut std::ffi::c_void, sigma: f64) -> i32;
    fn zimg_crop(img: *mut std::ffi::c_void, left: i32, top: i32, width: i32, height: i32) -> i32;
    fn zimg_rot(img: *mut std::ffi::c_void, angle: i32) -> i32;
    fn zimg_flip(img: *mut std::ffi::c_void, direction: i32) -> i32;
    fn zimg_release(img: *mut std::ffi::c_void);
    fn zimg_get_result() -> *mut std::ffi::c_void;
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
        match $val {
            zz_runtime::Value::Int(i) => i as *mut std::ffi::c_void,
            other => {
                return Err(zz_runtime::EvalError::TypeError {
                    expected: "int handle".to_string(),
                    got: format!("{other:?}"),
                    span: zz_runtime::Span::new(0, 0),
                });
            }
        }
    }};
}

macro_rules! as_float {
    ($val:expr) => {{
        match $val {
            zz_runtime::Value::Float(f) => f,
            zz_runtime::Value::Int(i) => i as f64,
            other => {
                return Err(zz_runtime::EvalError::TypeError {
                    expected: "float".to_string(),
                    got: format!("{other:?}"),
                    span: zz_runtime::Span::new(0, 0),
                });
            }
        }
    }};
}

macro_rules! as_int {
    ($val:expr) => {{
        match $val {
            zz_runtime::Value::Int(i) => i,
            other => {
                return Err(zz_runtime::EvalError::TypeError {
                    expected: "int".to_string(),
                    got: format!("{other:?}"),
                    span: zz_runtime::Span::new(0, 0),
                });
            }
        }
    }};
}

macro_rules! ok_int {
    ($val:expr) => {
        Ok(zz_runtime::Value::Int($val as i64))
    };
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
    Ok(zz_runtime::Value::Str(msg))
}

// ---------------------------------------------------------------------------
// Plugin registration entrypoint
// ---------------------------------------------------------------------------

type RegisterCallback = extern "C" fn(name: *const i8, arity: usize, f: zz_runtime::NativeFn);

#[no_mangle]
pub extern "C" fn zz_plugin_register(callback: RegisterCallback) {
    use std::ffi::CString;

    let name = CString::new("zimg.init").unwrap();
    callback(name.as_ptr(), 0, native_zimg_init);

    let name = CString::new("zimg.width").unwrap();
    callback(name.as_ptr(), 1, native_zimg_width);

    let name = CString::new("zimg.height").unwrap();
    callback(name.as_ptr(), 1, native_zimg_height);

    let name = CString::new("zimg.resize").unwrap();
    callback(name.as_ptr(), 2, native_zimg_resize);

    let name = CString::new("zimg.blur").unwrap();
    callback(name.as_ptr(), 2, native_zimg_blur);

    let name = CString::new("zimg.crop").unwrap();
    callback(name.as_ptr(), 5, native_zimg_crop);

    let name = CString::new("zimg.rot").unwrap();
    callback(name.as_ptr(), 2, native_zimg_rot);

    let name = CString::new("zimg.flip").unwrap();
    callback(name.as_ptr(), 2, native_zimg_flip);

    let name = CString::new("zimg.release").unwrap();
    callback(name.as_ptr(), 1, native_zimg_release);

    let name = CString::new("zimg.get_result").unwrap();
    callback(name.as_ptr(), 0, native_zimg_get_result);

    let name = CString::new("zimg.last_error").unwrap();
    callback(name.as_ptr(), 0, native_zimg_last_error);
}
