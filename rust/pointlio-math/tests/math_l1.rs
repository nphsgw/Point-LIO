//! The 14 in-scope tests from tests/math/math_tests.cpp, with unchanged limits.
//! Every test also checks its completed case count (once implementations exist).
mod support;

use pointlio_math::{
    esekf,
    linalg::{Mat30, Vec30},
    process, so3,
};
use std::{array::from_fn, f64::consts::PI};
use support::*;

// C++ So3Exp.MatchesAxisAngle
#[test]
// 3.14 is the original C++ test angle, deliberately distinct from PI.
#[allow(clippy::approx_constant)]
fn so3_exp_matches_axis_angle() {
    let mut rng = Rng::new(1);
    let mut count = 0;
    for angle in [2e-7, 1e-3, 0.5, 1.5, 3.0, 3.14] {
        for _ in 0..20 {
            let v = rng.vector(angle, angle);
            let actual = so3::exp(&v, 1.0);
            assert!(
                max_abs(&matrix_difference(&actual, &rodrigues(&v))) <= 8e-15,
                "angle {angle}"
            );
            count += 1;
        }
    }
    assert_eq!(count, 120);
}

// C++ So3Exp.BelowThresholdReturnsIdentityWithinAngle
#[test]
fn so3_exp_below_threshold_returns_identity_within_angle() {
    let mut rng = Rng::new(2);
    let mut count = 0;
    for _ in 0..20 {
        let v = rng.vector(1e-9, 1e-7);
        let actual = so3::exp(&v, 1.0);
        assert_eq!(actual, identity());
        assert!(max_abs(&matrix_difference(&actual, &rodrigues(&v))) <= norm(&v) * 1.0001);
        count += 1;
    }
    assert_eq!(count, 20);
}

// C++ So3Exp.IsRotation
#[test]
fn so3_exp_is_rotation() {
    let mut rng = Rng::new(3);
    let mut count = 0;
    for _ in 0..200 {
        let actual = so3::exp(&rng.vector(1e-6, PI), 1.0);
        let rtr = multiply(&transpose(&actual), &actual);
        assert!(max_abs(&matrix_difference(&rtr, &identity())) <= 8e-15);
        assert!((determinant(&actual) - 1.0).abs() <= 8e-15);
        count += 1;
    }
    assert_eq!(count, 200);
}

// C++ So3Log.InvertsExp
#[test]
fn so3_log_inverts_exp() {
    let mut rng = Rng::new(4);
    let mut count = 0;
    for _ in 0..200 {
        let v = rng.vector(1e-3, PI - 1e-3);
        let actual = so3::log(&rodrigues(&v));
        assert!(
            norm(&vector_difference(&actual, &v)) <= 1e-12,
            "angle {}",
            norm(&v)
        );
        count += 1;
    }
    assert_eq!(count, 200);
}

// C++ So3Log.SmallAngleBranchErrorBound
#[test]
fn so3_log_small_angle_branch_error_bound() {
    let mut rng = Rng::new(5);
    let mut count = 0;
    for _ in 0..100 {
        let v = rng.vector(1e-6, 0.999e-3);
        let actual = so3::log(&rodrigues(&v));
        let t = norm(&v);
        assert!(
            norm(&vector_difference(&actual, &v)) <= t * t * t / 6.0 * 1.01 + 1e-18,
            "angle {t}"
        );
        count += 1;
    }
    assert_eq!(count, 100);
}

// C++ So3Log.FiniteNearPi (BL-008)
#[test]
fn so3_log_finite_near_pi() {
    let mut rng = Rng::new(6);
    let mut count = 0;
    for d in [1e-4, 1e-7, 1e-10, 0.0] {
        for _ in 0..20 {
            let v = rng.vector(PI - d, PI - d);
            let actual = so3::log(&rodrigues(&v));
            assert!(actual.iter().all(|x| x.is_finite()), "pi - {d}");
            assert!(
                max_abs(&matrix_difference(&rodrigues(&actual), &rodrigues(&v))) <= 1e-6,
                "pi - {d}"
            );
            count += 1;
        }
    }
    assert_eq!(count, 80);
}

// C++ So3Log.AccurateAroundNearPiBranch (BL-008)
#[test]
fn so3_log_accurate_around_near_pi_branch() {
    let mut rng = Rng::new(14);
    let mut count = 0;
    for d in [1e-3, 4e-4, 3.2e-4, 3.1e-4, 2e-4, 1e-5, 1e-8, 1e-12] {
        for _ in 0..20 {
            let v = rng.vector(PI - d, PI - d);
            let actual = so3::log(&rodrigues(&v));
            let bound = if d < 3.16e-4 {
                1e-12
            } else {
                10.0 * f64::EPSILON / (d * d)
            };
            assert!(norm(&vector_difference(&actual, &v)) <= bound, "pi - {d}");
            count += 1;
        }
    }
    assert_eq!(count, 160);
}

// C++ So3Boxplus.IsRightPerturbation
#[test]
fn so3_boxplus_is_right_perturbation() {
    let mut rng = Rng::new(7);
    let mut count = 0;
    for _ in 0..50 {
        let original = rodrigues(&rng.vector(0.0, 3.0));
        let delta = rng.vector(1e-3, 1.0);
        let mut actual = original;
        so3::boxplus(&mut actual, &delta, 1.0);
        let increment = rodrigues(&delta);
        assert!(
            max_abs(&matrix_difference(
                &actual,
                &multiply(&original, &increment)
            )) <= 8e-15
        );
        assert!(
            max_abs(&matrix_difference(
                &actual,
                &multiply(&increment, &original)
            )) > 1e-6
        );
        count += 1;
    }
    assert_eq!(count, 50);
}

// C++ So3Boxminus.InvertsBoxplus
#[test]
fn so3_boxminus_inverts_boxplus() {
    let mut rng = Rng::new(8);
    let mut count = 0;
    for _ in 0..100 {
        let original = rodrigues(&rng.vector(0.0, 3.0));
        let delta = rng.vector(1e-3, 3.0);
        let mut incremented = original;
        so3::boxplus(&mut incremented, &delta, 1.0);
        let actual = so3::boxminus(&incremented, &original);
        assert!(norm(&vector_difference(&actual, &delta)) <= 1e-12);
        count += 1;
    }
    assert_eq!(count, 100);
}

// C++ StateBoxminus.InvertsBoxplusForAllComponents
#[test]
fn state_boxminus_inverts_boxplus_for_all_components() {
    let mut rng = Rng::new(9);
    let mut count = 0;
    for _ in 0..50 {
        let x = rng.state();
        let delta: Vec30 = from_fn(|_| 0.3 * rng.normal());
        let mut incremented = x.clone();
        incremented.boxplus(&delta, 1.0);
        let actual = incremented.boxminus(&x);
        assert!(norm(&vector_difference(&actual, &delta)) <= 1e-12);
        count += 1;
    }
    assert_eq!(count, 50);
}

// C++ AMatrix.IsLeftJacobianOfExp
#[test]
fn a_matrix_is_left_jacobian_of_exp() {
    let mut rng = Rng::new(10);
    let h = 1e-6;
    let (mut count, mut columns) = (0, 0);
    for _ in 0..50 {
        let v = rng.vector(1e-3, 2.5);
        let rt = transpose(&rodrigues(&v));
        let mut numeric = [[0.0; 3]; 3];
        for j in 0..3 {
            let mut plus = v;
            let mut minus = v;
            plus[j] += h;
            minus[j] -= h;
            let lp = log_reference(&multiply(&rodrigues(&plus), &rt));
            let lm = log_reference(&multiply(&rodrigues(&minus), &rt));
            for (r, row) in numeric.iter_mut().enumerate() {
                row[j] = (lp[r] - lm[r]) / (2.0 * h);
            }
            columns += 1;
        }
        let actual = so3::a_matrix(&v);
        assert!(
            max_abs(&matrix_difference(&actual, &numeric)) <= 1e-8,
            "angle {}",
            norm(&v)
        );
        count += 1;
    }
    assert_eq!((count, columns), (50, 150));
}

// C++ ProcessModel.JacobianMatchesNumeric
#[test]
fn process_model_jacobian_matches_numeric() {
    let mut rng = Rng::new(11);
    let h = 1e-6;
    let (mut count, mut columns) = (0, 0);
    for _ in 0..20 {
        let x = rng.state();
        let mut numeric = [[0.0; 30]; 30];
        for j in 0..30 {
            let xp = perturb(&x, &unit(j, h));
            let xm = perturb(&x, &unit(j, -h));
            let fp = process::f_output(&xp);
            let fm = process::f_output(&xm);
            for (r, row) in numeric.iter_mut().enumerate() {
                row[j] = (fp[r] - fm[r]) / (2.0 * h);
            }
            columns += 1;
        }
        let actual = process::df_dx_output(&x);
        assert!(max_abs(&matrix_difference(&actual, &numeric)) <= 1e-7);
        count += 1;
    }
    assert_eq!((count, columns), (20, 600));
}

// C++ Predict.CovarianceMatchesNumericJacobian
#[test]
fn predict_covariance_matches_numeric_jacobian() {
    let mut rng = Rng::new(12);
    let h = 1e-6;
    let (mut count, mut columns) = (0, 0);
    for dt in [0.005, 0.1] {
        for _ in 0..10 {
            let x = rng.state();
            let p = covariance(&mut rng, 1e-3);
            let q: Mat30 = from_fn(|r| from_fn(|c| if r == c { rng.normal().abs() } else { 0.0 }));
            let y = propagate(&x, dt);
            let mut f = [[0.0; 30]; 30];
            for j in 0..30 {
                let yp = propagate(&perturb(&x, &unit(j, h)), dt);
                let ym = propagate(&perturb(&x, &unit(j, -h)), dt);
                let dp = yp.boxminus(&y);
                let dm = ym.boxminus(&y);
                for (r, row) in f.iter_mut().enumerate() {
                    row[j] = (dp[r] - dm[r]) / (2.0 * h);
                }
                columns += 1;
            }
            let fpf = multiply(&multiply(&f, &p), &transpose(&f));
            let expected: Mat30 = from_fn(|r| from_fn(|c| fpf[r][c] + (q[r][c] * dt) * dt));
            let mut actual_x = x.clone();
            let mut actual_p = p;
            esekf::predict(&mut actual_x, &mut actual_p, &q, dt, false, true);
            assert!(
                max_abs(&matrix_difference(&actual_p, &expected)) <= 1e-7 * max_abs(&expected),
                "dt {dt}"
            );
            assert_eq!(max_abs_vector(&actual_x.boxminus(&x)), 0.0);
            count += 1;
        }
    }
    assert_eq!((count, columns), (20, 600));
}

// C++ Predict.CovarianceStaysSymmetricPositive
#[test]
fn predict_covariance_stays_symmetric_positive() {
    let mut rng = Rng::new(13);
    let mut count = 0;
    for _ in 0..20 {
        let mut p = covariance(&mut rng, 0.0);
        let q: Mat30 = from_fn(|r| from_fn(|c| if r == c { 1e-4 } else { 0.0 }));
        let mut x = rng.state();
        esekf::predict(&mut x, &mut p, &q, 0.01, false, true);
        let m = max_abs(&p);
        assert!(max_abs(&matrix_difference(&p, &transpose(&p))) <= 1e-13 * m);
        let symmetric: Mat30 = from_fn(|r| from_fn(|c| (p[r][c] + p[c][r]) / 2.0));
        assert!(min_eigenvalue(symmetric) >= -1e-12 * m);
        count += 1;
    }
    assert_eq!(count, 20);
}
