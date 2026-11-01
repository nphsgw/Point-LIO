//! SO3 operations, retaining B's expressions, branches and measured summation.
use crate::{
    libm,
    linalg::{self, Mat3, Vec3},
};
use std::array::from_fn;

pub fn exp(v: &Vec3, scale: f64) -> Mat3 {
    let ang = v.map(|x| x * scale);
    let n = linalg::norm3(&ang);
    if n > 1e-7 {
        rodrigues(&ang, n)
    } else {
        linalg::identity3()
    }
}

fn rodrigues(ang: &Vec3, norm: f64) -> Mat3 {
    let k = linalg::skew(&ang.map(|x| x / norm));
    let (sin, cos) = libm::sin_cos(norm);
    let quadratic = linalg::scale_mul3(1.0 - cos, &k, &k);
    let identity = linalg::identity3();
    from_fn(|r| from_fn(|c| (identity[r][c] + sin * k[r][c]) + quadratic[r][c]))
}

pub fn log(rotation: &Mat3) -> Vec3 {
    let theta = if linalg::trace3(rotation) > 3.0 - 1e-6 {
        0.0
    } else {
        (0.5 * (linalg::trace3(rotation) - 1.0)).acos()
    };
    let k = [
        rotation[2][1] - rotation[1][2],
        rotation[0][2] - rotation[2][0],
        rotation[1][0] - rotation[0][1],
    ];
    let c = 0.5 * (linalg::trace3(rotation) - 1.0);
    if c < -1.0 + 5e-8 {
        let identity = linalg::identity3();
        // Do not omit c*0 on off-diagonals: it changes signed zero.
        let s: Mat3 = from_fn(|r| {
            from_fn(|col| 0.5 * (rotation[r][col] + rotation[col][r]) - c * identity[r][col])
        });
        let mut i = 0;
        for j in 1..3 {
            if s[j][j] > s[i][i] {
                i = j;
            }
        }
        let mut axis: Vec3 = from_fn(|r| s[r][i]);
        let z = linalg::dot3(&axis, &axis);
        if z > 0.0 {
            let norm = z.sqrt();
            axis = axis.map(|x| x / norm);
        }
        if linalg::dot3(&axis, &k) < 0.0 {
            axis = axis.map(|x| -x);
        }
        let angle = (0.5 * linalg::norm3(&k)).atan2(c);
        axis.map(|x| angle * x)
    } else if theta.abs() < 0.001 {
        k.map(|x| 0.5 * x)
    } else {
        // BL-011 in B: retain the small-angle decision above, but recover the
        // ordinary angle and normalize using the same antisymmetric part.
        let k_norm = linalg::norm3(&k);
        let scale = (0.5 * k_norm).atan2(c) / k_norm;
        k.map(|x| scale * x)
    }
}

pub fn boxplus(rotation: &mut Mat3, delta: &Vec3, scale: f64) {
    *rotation = linalg::mul3(rotation, &exp(delta, scale));
}

pub fn boxminus(rotation: &Mat3, reference: &Mat3) -> Vec3 {
    log(&linalg::transpose_mul3(reference, rotation))
}

pub fn a_matrix(v: &Vec3) -> Mat3 {
    let squared_norm = linalg::dot3(v, v);
    let norm = squared_norm.sqrt();
    let identity = linalg::identity3();
    if norm < 1e-11 {
        identity
    } else {
        let k = linalg::skew(v);
        let (sin, cos) = libm::sin_cos(norm);
        let s1 = (1.0 - cos) / squared_norm;
        let s2 = (1.0 - sin / norm) / squared_norm;
        let quadratic = linalg::scale_mul3(s2, &k, &k);
        from_fn(|r| from_fn(|c| (identity[r][c] + s1 * k[r][c]) + quadratic[r][c]))
    }
}

pub fn exp3(v1: f64, v2: f64, v3: f64) -> Mat3 {
    let ang = [v1, v2, v3];
    let norm = linalg::norm3(&ang);
    if norm > 1e-5 {
        rodrigues(&ang, norm)
    } else {
        linalg::identity3()
    }
}

/// Quaternion coefficients in xyzw order, without normalization.
pub fn quat_from_matrix(rotation: &Mat3) -> [f64; 4] {
    // Eigen 3.3.7 Geometry/Quaternion.h quaternionbase_assign_impl<Other,3,3>.
    let trace = linalg::trace3(rotation);
    let mut q = [0.0; 4];
    if trace > 0.0 {
        let root = (trace + 1.0).sqrt();
        q[3] = 0.5 * root;
        let t = 0.5 / root;
        q[0] = (rotation[2][1] - rotation[1][2]) * t;
        q[1] = (rotation[0][2] - rotation[2][0]) * t;
        q[2] = (rotation[1][0] - rotation[0][1]) * t;
    } else {
        let mut i = 0;
        if rotation[1][1] > rotation[0][0] {
            i = 1;
        }
        if rotation[2][2] > rotation[i][i] {
            i = 2;
        }
        let j = (i + 1) % 3;
        let k = (j + 1) % 3;
        let root = ((rotation[i][i] - rotation[j][j]) - rotation[k][k] + 1.0).sqrt();
        q[i] = 0.5 * root;
        let t = 0.5 / root;
        q[3] = (rotation[k][j] - rotation[j][k]) * t;
        q[j] = (rotation[j][i] + rotation[i][j]) * t;
        q[k] = (rotation[k][i] + rotation[i][k]) * t;
    }
    q
}
