//! SO3 API. Bodies intentionally remain unimplemented for plan step 4 (Red).
use crate::linalg::{Mat3, Vec3};

pub fn exp(_v: &Vec3, _scale: f64) -> Mat3 {
    todo!("so3::exp")
}

pub fn log(_rotation: &Mat3) -> Vec3 {
    todo!("so3::log")
}

pub fn boxplus(_rotation: &mut Mat3, _delta: &Vec3, _scale: f64) {
    todo!("so3::boxplus")
}

pub fn boxminus(_rotation: &Mat3, _reference: &Mat3) -> Vec3 {
    todo!("so3::boxminus")
}

pub fn a_matrix(_v: &Vec3) -> Mat3 {
    todo!("so3::a_matrix")
}

pub fn exp3(_v1: f64, _v2: f64, _v3: f64) -> Mat3 {
    todo!("so3::exp3")
}

/// Quaternion coefficients in xyzw order, without normalization.
pub fn quat_from_matrix(_rotation: &Mat3) -> [f64; 4] {
    todo!("so3::quat_from_matrix")
}
