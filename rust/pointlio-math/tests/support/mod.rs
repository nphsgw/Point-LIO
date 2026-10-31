//! Independent L1 references. Never call production numeric operations here,
//! except in the explicitly named perturbation/propagation helpers at the end.
use pointlio_math::{
    linalg::{Mat3, Mat30, Vec3, Vec30},
    process,
    state::StateOutput,
};
use std::array::from_fn;

pub type Matrix<const R: usize, const C: usize> = [[f64; C]; R];

pub fn identity<const N: usize>() -> Matrix<N, N> {
    from_fn(|r| from_fn(|c| if r == c { 1.0 } else { 0.0 }))
}

pub fn transpose<const R: usize, const C: usize>(a: &Matrix<R, C>) -> Matrix<C, R> {
    from_fn(|r| from_fn(|c| a[c][r]))
}

pub fn multiply<const R: usize, const K: usize, const C: usize>(
    a: &Matrix<R, K>,
    b: &Matrix<K, C>,
) -> Matrix<R, C> {
    from_fn(|r| from_fn(|c| (0..K).fold(0.0, |sum, k| sum + a[r][k] * b[k][c])))
}

pub fn vector_scale<const N: usize>(v: &[f64; N], s: f64) -> [f64; N] {
    from_fn(|i| v[i] * s)
}

pub fn vector_difference<const N: usize>(a: &[f64; N], b: &[f64; N]) -> [f64; N] {
    from_fn(|i| a[i] - b[i])
}

pub fn norm<const N: usize>(v: &[f64; N]) -> f64 {
    v.iter().fold(0.0, |sum, x| sum + x * x).sqrt()
}

pub fn max_abs_vector<const N: usize>(v: &[f64; N]) -> f64 {
    assert!(N > 0);
    v.iter().fold(0.0, |max: f64, x| {
        // f64::max alone would silently ignore NaN and could pass a bad result.
        assert!(x.is_finite(), "nonfinite vector component: {x}");
        max.max(x.abs())
    })
}

pub fn max_abs<const R: usize, const C: usize>(a: &Matrix<R, C>) -> f64 {
    assert!(R > 0 && C > 0);
    a.iter()
        .fold(0.0, |max: f64, row| max.max(max_abs_vector(row)))
}

pub fn matrix_difference<const R: usize, const C: usize>(
    a: &Matrix<R, C>,
    b: &Matrix<R, C>,
) -> Matrix<R, C> {
    from_fn(|r| from_fn(|c| a[r][c] - b[r][c]))
}

pub fn determinant(a: &Mat3) -> f64 {
    a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
        - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
        + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0])
}

/// cos(t) I + sin(t) [a]x + (1-cos(t)) aa^T, unlike the production K*K form.
pub fn rodrigues(v: &Vec3) -> Mat3 {
    let t = norm(v);
    if t == 0.0 {
        return identity();
    }
    let a: Vec3 = from_fn(|i| v[i] / t);
    let k = [[0.0, -a[2], a[1]], [a[2], 0.0, -a[0]], [-a[1], a[0], 0.0]];
    let (s, c) = (t.sin(), t.cos());
    from_fn(|r| {
        from_fn(|col| {
            let diagonal = if r == col { c } else { 0.0 };
            (diagonal + s * k[r][col]) + (1.0 - c) * a[r] * a[col]
        })
    })
}

/// Quaternion route to angle-axis (the C++ test uses Eigen::AngleAxisd).
/// Does not use production Log, Exp, quaternion conversion, or linalg functions.
pub fn log_reference(r: &Mat3) -> Vec3 {
    let trace = r[0][0] + r[1][1] + r[2][2];
    let mut q = [0.0; 4]; // xyzw
    if trace > 0.0 {
        let t = (trace + 1.0).sqrt();
        q[3] = 0.5 * t;
        let s = 0.5 / t;
        q[0] = (r[2][1] - r[1][2]) * s;
        q[1] = (r[0][2] - r[2][0]) * s;
        q[2] = (r[1][0] - r[0][1]) * s;
    } else {
        let mut i = 0;
        if r[1][1] > r[0][0] {
            i = 1;
        }
        if r[2][2] > r[i][i] {
            i = 2;
        }
        let j = (i + 1) % 3;
        let k = (j + 1) % 3;
        let t = (r[i][i] - r[j][j] - r[k][k] + 1.0).sqrt();
        q[i] = 0.5 * t;
        let s = 0.5 / t;
        q[j] = (r[j][i] + r[i][j]) * s;
        q[k] = (r[k][i] + r[i][k]) * s;
        q[3] = (r[k][j] - r[j][k]) * s;
    }
    if q[3] < 0.0 {
        q = q.map(|v| -v);
    }
    let axis = [q[0], q[1], q[2]];
    let n = norm(&axis);
    if n == 0.0 {
        return [0.0; 3];
    }
    let angle = 2.0 * n.atan2(q[3]);
    vector_scale(&axis, angle / n)
}

/// Deterministic SplitMix64 and Box-Muller; L1 need not share C++'s random stream.
pub struct Rng(u64);
impl Rng {
    pub fn new(seed: u64) -> Self {
        Self(seed)
    }
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9e3779b97f4a7c15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
        z ^ (z >> 31)
    }
    pub fn uniform(&mut self, lo: f64, hi: f64) -> f64 {
        let u = (self.next() >> 11) as f64 / 9007199254740992.0;
        lo + (hi - lo) * u
    }
    pub fn normal(&mut self) -> f64 {
        let u = 1.0 - self.uniform(0.0, 1.0);
        let angle = std::f64::consts::TAU * self.uniform(0.0, 1.0);
        (-2.0 * u.ln()).sqrt() * angle.cos()
    }
    pub fn vector(&mut self, lo: f64, hi: f64) -> Vec3 {
        let a = from_fn(|_| self.normal());
        let n = norm(&a);
        assert!(n > 0.0 && n.is_finite());
        vector_scale(&a, self.uniform(lo, hi) / n)
    }
    pub fn state(&mut self) -> StateOutput {
        StateOutput {
            pos: self.vector(0.0, 10.0),
            rot: rodrigues(&self.vector(0.0, 3.0)),
            offset_r_l_i: rodrigues(&self.vector(0.0, 0.5)),
            offset_t_l_i: self.vector(0.0, 0.5),
            vel: self.vector(0.0, 3.0),
            omg: self.vector(0.1, 3.0),
            acc: self.vector(0.1, 15.0),
            gravity: self.vector(9.7, 9.9),
            bg: self.vector(0.0, 0.05),
            ba: self.vector(0.0, 0.3),
        }
    }
}

/// Cyclic Jacobi reference for a real symmetric matrix. Convergence is required;
/// the EPSILON stopping rule belongs to the reference solver, not the L1 limits.
// Coupled row/column updates use explicit matrix indices to preserve symmetry.
#[allow(clippy::needless_range_loop)]
pub fn min_eigenvalue<const N: usize>(mut a: Matrix<N, N>) -> f64 {
    assert!(N > 0);
    max_abs(&a); // reject nonfinite inputs
    for _sweep in 0..100 {
        let scale = (0..N).fold(0.0_f64, |m, i| m.max(a[i][i].abs()));
        let off = (0..N)
            .flat_map(|p| (p + 1..N).map(move |q| (p, q)))
            .fold(0.0_f64, |m, (p, q)| m.max(a[p][q].abs()));
        if off <= f64::EPSILON * scale {
            return (0..N).map(|i| a[i][i]).fold(f64::INFINITY, f64::min);
        }
        for p in 0..N {
            for q in p + 1..N {
                let apq = a[p][q];
                if apq == 0.0 {
                    continue;
                }
                let tau = (a[q][q] - a[p][p]) / (2.0 * apq);
                let t = tau.signum() / (tau.abs() + tau.hypot(1.0));
                let c = 1.0 / (1.0 + t * t).sqrt();
                let s = t * c;
                a[p][p] -= t * apq;
                a[q][q] += t * apq;
                a[p][q] = 0.0;
                a[q][p] = 0.0;
                for k in 0..N {
                    if k == p || k == q {
                        continue;
                    }
                    let (akp, akq) = (a[k][p], a[k][q]);
                    a[k][p] = c * akp - s * akq;
                    a[p][k] = a[k][p];
                    a[k][q] = s * akp + c * akq;
                    a[q][k] = a[k][q];
                }
            }
        }
    }
    panic!("reference Jacobi solver did not converge");
}

// These helpers intentionally exercise the production state operations, as the
// original C++ finite-difference tests do. They are not independent oracles for
// those state operations; the Exp/Log/boxplus tests cover that separately.
pub fn perturb(x: &StateOutput, d: &Vec30) -> StateOutput {
    let mut y = x.clone();
    y.boxplus(d, 1.0);
    y
}

pub fn propagate(x: &StateOutput, dt: f64) -> StateOutput {
    let f = process::f_output(x);
    let mut y = x.clone();
    y.oplus(&f, dt);
    y
}

pub fn unit(index: usize, scale: f64) -> Vec30 {
    from_fn(|i| if i == index { scale } else { 0.0 })
}

pub fn covariance(rng: &mut Rng, diagonal: f64) -> Mat30 {
    let a: Mat30 = from_fn(|_| from_fn(|_| rng.normal()));
    let aat = multiply(&a, &transpose(&a));
    from_fn(|r| from_fn(|c| aat[r][c] / 30.0 + if r == c { diagonal } else { 0.0 }))
}
