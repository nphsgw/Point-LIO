//! B's output-state process model (Estimator.cpp). The input object is unused.
use crate::{
    linalg::{self, Mat30, Vec30},
    state::StateOutput,
};

pub fn f_output(x: &StateOutput) -> Vec30 {
    let mut f = [0.0; 30];
    let inertial_acc = linalg::mul_vec3(&x.rot, &x.acc);
    for i in 0..3 {
        f[i] = x.vel[i];
        f[3 + i] = x.omg[i];
        f[12 + i] = inertial_acc[i] + x.gravity[i];
    }
    f
}

pub fn df_dx_output(x: &StateOutput) -> Mat30 {
    let mut df = [[0.0; 30]; 30];
    let identity = linalg::identity3();
    let rotation_acc = linalg::mul3(&x.rot.map(|row| row.map(|v| -v)), &linalg::skew(&x.acc));
    for r in 0..3 {
        for c in 0..3 {
            df[r][12 + c] = identity[r][c];
            df[3 + r][15 + c] = identity[r][c];
            df[12 + r][3 + c] = rotation_acc[r][c];
            df[12 + r][18 + c] = x.rot[r][c];
            df[12 + r][21 + c] = identity[r][c];
        }
    }
    df
}
