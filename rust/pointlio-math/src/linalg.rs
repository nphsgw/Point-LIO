//! Fixed-size row-major storage; operation order follows the measured Eigen V0.
use std::array::from_fn;

pub type Vec3 = [f64; 3];
pub type Mat3 = [[f64; 3]; 3];
pub type Vec30 = [f64; 30];
pub type Mat30 = [[f64; 30]; 30];

pub(crate) fn identity3() -> Mat3 {
    from_fn(|r| from_fn(|c| if r == c { 1.0 } else { 0.0 }))
}

pub(crate) fn dot3(a: &Vec3, b: &Vec3) -> f64 {
    (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2]
}

pub(crate) fn norm3(v: &Vec3) -> f64 {
    dot3(v, v).sqrt()
}

pub(crate) fn trace3(a: &Mat3) -> f64 {
    a[0][0] + (a[1][1] + a[2][2])
}

pub(crate) fn skew(v: &Vec3) -> Mat3 {
    [[0.0, -v[2], v[1]], [v[2], 0.0, -v[0]], [-v[1], v[0], 0.0]]
}

fn row_sum(r: usize, x: f64, y: f64, z: f64) -> f64 {
    if r == 2 { x + (y + z) } else { (x + y) + z }
}

pub(crate) fn mul3(a: &Mat3, b: &Mat3) -> Mat3 {
    from_fn(|r| from_fn(|c| row_sum(r, a[r][0] * b[0][c], a[r][1] * b[1][c], a[r][2] * b[2][c])))
}

pub(crate) fn scale_mul3(s: f64, a: &Mat3, b: &Mat3) -> Mat3 {
    // s*A*B scales the left operand before the product, not the result.
    mul3(&a.map(|row| row.map(|x| s * x)), b)
}

pub(crate) fn transpose_mul3(a: &Mat3, b: &Mat3) -> Mat3 {
    // Eigen's transpose-product uses left association in all three rows.
    from_fn(|r| from_fn(|c| (a[0][r] * b[0][c] + a[1][r] * b[1][c]) + a[2][r] * b[2][c]))
}

pub(crate) fn mul_vec3(a: &Mat3, v: &Vec3) -> Vec3 {
    from_fn(|r| row_sum(r, a[r][0] * v[0], a[r][1] * v[1], a[r][2] * v[2]))
}

pub(crate) fn mul30(a: &Mat30, b: &Mat30) -> Mat30 {
    // Preserve (F*P)*F^T and ascending k. Eigen's blocked GEMM order is the
    // preregistered difference for predict_cov; do not add FMA or symmetrize.
    from_fn(|r| {
        from_fn(|c| {
            let mut sum = a[r][0] * b[0][c];
            for k in 1..30 {
                sum += a[r][k] * b[k][c];
            }
            sum
        })
    })
}

pub(crate) fn transpose30(a: &Mat30) -> Mat30 {
    from_fn(|r| from_fn(|c| a[c][r]))
}
