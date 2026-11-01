//! Match GCC V0's combined sin/cos calls in glibc, in debug and release.
#![allow(unsafe_code)]

// glibc ABI: void sincos(double, double *, double *), linked explicitly to libm.
#[link(name = "m")]
unsafe extern "C" {
    fn sincos(x: f64, sin: *mut f64, cos: *mut f64);
}

pub(crate) fn sin_cos(x: f64) -> (f64, f64) {
    let (mut sin, mut cos) = (0.0, 0.0);
    // SAFETY: distinct initialized, aligned f64 locals remain live throughout
    // the call; glibc writes the two doubles and does not retain either pointer.
    // The C ABI and binary64 double representation match the pinned x86-64 image.
    unsafe {
        sincos(x, &mut sin, &mut cos);
    }
    (sin, cos)
}
