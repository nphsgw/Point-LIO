//! State storage in C++ declaration order; numerical methods are Red stubs.
use crate::linalg::{Mat3, Vec3, Vec30};

#[derive(Clone, Debug, PartialEq)]
pub struct StateOutput {
    pub pos: Vec3,
    pub rot: Mat3,
    pub offset_r_l_i: Mat3,
    pub offset_t_l_i: Vec3,
    pub vel: Vec3,
    pub omg: Vec3,
    pub acc: Vec3,
    pub gravity: Vec3,
    pub bg: Vec3,
    pub ba: Vec3,
}

impl StateOutput {
    pub fn boxplus(&mut self, _delta: &Vec30, _scale: f64) {
        todo!("StateOutput::boxplus")
    }

    pub fn boxminus(&self, _reference: &Self) -> Vec30 {
        todo!("StateOutput::boxminus")
    }

    pub fn oplus(&mut self, _delta: &Vec30, _scale: f64) {
        todo!("StateOutput::oplus")
    }
}
