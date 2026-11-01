//! Prediction API. The two flags retain the C++ interface and semantics.
use crate::{
    linalg::{self, Mat30, Vec3},
    process, so3,
    state::StateOutput,
};
use std::array::from_fn;

pub fn predict(
    x: &mut StateOutput,
    p: &mut Mat30,
    q: &Mat30,
    dt: f64,
    predict_state: bool,
    prop_cov: bool,
) {
    if predict_state {
        x.oplus(&process::f_output(x), dt);
    }
    if prop_cov {
        // Deliberately recompute from the updated state when both flags hold.
        let f = process::f_output(x);
        let df = process::df_dx_output(x);
        let mut final_df = df;
        let mut transition: Mat30 = from_fn(|r| from_fn(|c| if r == c { 1.0 } else { 0.0 }));
        for start in [3, 6] {
            // Retain B's explicit (-1 * f) * dt expression.
            #[allow(clippy::neg_multiply)]
            let seg: Vec3 = from_fn(|i| (-1.0 * f[start + i]) * dt);
            let rotation = so3::exp(&seg, 1.0);
            let a = so3::a_matrix(&seg);
            for r in 0..3 {
                for c in 0..3 {
                    transition[start + r][start + c] = rotation[r][c];
                }
            }
            for c in 0..30 {
                let column: Vec3 = from_fn(|r| df[start + r][c]);
                let transformed = linalg::mul_vec3(&a, &column);
                for r in 0..3 {
                    final_df[start + r][c] = transformed[r];
                }
            }
        }
        for r in 0..30 {
            for c in 0..30 {
                transition[r][c] += final_df[r][c] * dt;
            }
        }
        let propagated = linalg::mul30(
            &linalg::mul30(&transition, p),
            &linalg::transpose30(&transition),
        );
        let dt_squared = dt * dt;
        *p = from_fn(|r| from_fn(|c| propagated[r][c] + q[r][c] * dt_squared));
    }
}
