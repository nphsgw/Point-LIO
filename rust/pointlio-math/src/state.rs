//! State in C++ declaration order. Error and tangent coordinates share 30 slots.
use crate::{
    linalg::{Mat3, Vec3, Vec30},
    so3,
};

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
    pub fn boxplus(&mut self, delta: &Vec30, scale: f64) {
        add(&mut self.pos, delta, 0, scale);
        so3::boxplus(&mut self.rot, &segment(delta, 3), scale);
        so3::boxplus(&mut self.offset_r_l_i, &segment(delta, 6), scale);
        add(&mut self.offset_t_l_i, delta, 9, scale);
        add(&mut self.vel, delta, 12, scale);
        add(&mut self.omg, delta, 15, scale);
        add(&mut self.acc, delta, 18, scale);
        add(&mut self.gravity, delta, 21, scale);
        add(&mut self.bg, delta, 24, scale);
        add(&mut self.ba, delta, 27, scale);
    }

    pub fn boxminus(&self, reference: &Self) -> Vec30 {
        let mut result = [0.0; 30];
        for (offset, value, other) in [
            (0, &self.pos, &reference.pos),
            (9, &self.offset_t_l_i, &reference.offset_t_l_i),
            (12, &self.vel, &reference.vel),
            (15, &self.omg, &reference.omg),
            (18, &self.acc, &reference.acc),
            (21, &self.gravity, &reference.gravity),
            (24, &self.bg, &reference.bg),
            (27, &self.ba, &reference.ba),
        ] {
            for i in 0..3 {
                result[offset + i] = value[i] - other[i];
            }
        }
        result[3..6].copy_from_slice(&so3::boxminus(&self.rot, &reference.rot));
        result[6..9].copy_from_slice(&so3::boxminus(&self.offset_r_l_i, &reference.offset_r_l_i));
        result
    }

    pub fn oplus(&mut self, delta: &Vec30, scale: f64) {
        // For state_output, DIM == DOF for every component, including both SO3s.
        self.boxplus(delta, scale);
    }
}

fn segment(delta: &Vec30, start: usize) -> Vec3 {
    [delta[start], delta[start + 1], delta[start + 2]]
}

fn add(value: &mut Vec3, delta: &Vec30, start: usize, scale: f64) {
    for i in 0..3 {
        value[i] += scale * delta[start + i];
    }
}
