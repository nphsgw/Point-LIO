//! Process model API; implementation follows L1 Red verification.
use crate::{
    linalg::{Mat30, Vec30},
    state::StateOutput,
};

pub fn f_output(_x: &StateOutput) -> Vec30 {
    todo!("process::f_output")
}

pub fn df_dx_output(_x: &StateOutput) -> Mat30 {
    todo!("process::df_dx_output")
}
