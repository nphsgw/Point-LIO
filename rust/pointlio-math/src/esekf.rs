//! Prediction API. The two flags retain the C++ interface and semantics.
use crate::{linalg::Mat30, state::StateOutput};

pub fn predict(
    _x: &mut StateOutput,
    _p: &mut Mat30,
    _q: &Mat30,
    _dt: f64,
    _predict_state: bool,
    _prop_cov: bool,
) {
    todo!("esekf::predict")
}
