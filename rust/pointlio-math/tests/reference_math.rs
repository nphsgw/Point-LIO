//! Check the independent references against analytic cases before using them.
// This suite only uses the reference subset of the shared L1 helpers.
#[allow(dead_code)]
mod support;

use std::f64::consts::{FRAC_PI_2, PI};
use support::*;

#[test]
fn rotation_references_match_known_axis_rotations() {
    assert_eq!(rodrigues(&[0.0; 3]), identity());
    assert_eq!(log_reference(&identity()), [0.0; 3]);
    let mut count = 0;
    for axis in 0..3 {
        for angle in [1e-6, 0.5, FRAC_PI_2, PI - 1e-7] {
            let mut v = [0.0; 3];
            v[axis] = angle;
            let r = rodrigues(&v);
            assert!(
                max_abs(&matrix_difference(
                    &multiply(&transpose(&r), &r),
                    &identity()
                )) <= 8e-15
            );
            assert!(norm(&vector_difference(&log_reference(&r), &v)) <= 1e-12);
            count += 1;
        }
    }
    assert_eq!(count, 12);
}

#[test]
fn jacobi_reference_matches_known_spectra() {
    assert_eq!(min_eigenvalue([[0.0; 3]; 3]), 0.0);
    assert_eq!(min_eigenvalue([[3.0, 0.0], [0.0, -2.0]]), -2.0);
    // [[2,1],[1,2]] has eigenvalues 1 and 3, plus decoupled eigenvalues 4..31.
    let mut a = [[0.0; 30]; 30];
    for (i, row) in a.iter_mut().enumerate() {
        row[i] = i as f64 + 2.0;
    }
    a[1][1] = 2.0;
    a[0][1] = 1.0;
    a[1][0] = 1.0;
    assert!((min_eigenvalue(a) - 1.0).abs() <= 1e-12);

    // Dense rank-one update: d*I + v*v^T has minimum eigenvalue d (N > 1).
    let v: [f64; 30] = std::array::from_fn(|i| (i + 1) as f64 / 30.0);
    let mut count = 0;
    for d in [2.0, -2.0] {
        let dense: [[f64; 30]; 30] = std::array::from_fn(|r| {
            std::array::from_fn(|c| v[r] * v[c] + if r == c { d } else { 0.0 })
        });
        assert!((min_eigenvalue(dense) - d).abs() <= 1e-12);
        count += 1;
    }
    assert_eq!(count, 2);
}

#[test]
fn reference_helpers_reject_nonfinite_data() {
    assert!(std::panic::catch_unwind(|| max_abs(&[[f64::NAN]])).is_err());
    assert!(std::panic::catch_unwind(|| max_abs_vector(&[f64::INFINITY])).is_err());
    assert!(std::panic::catch_unwind(|| min_eigenvalue([[f64::NAN]])).is_err());
}
