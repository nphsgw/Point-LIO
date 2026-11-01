//! Run the Rust components on B inputs; never use B output values in calculation.
use pointlio_math::{
    esekf,
    golden::{COMPONENTS, Record, decode, encode},
    linalg::{Mat3, Mat30},
    process, so3,
    state::StateOutput,
};
use std::{env, fs, io::Write, path::Path, process::ExitCode};

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = env::args_os().skip(1).collect();
    if args.len() != 2 {
        return Err("usage: math_golden <baseline directory> <existing output directory>".into());
    }
    let baseline = Path::new(&args[0]).canonicalize()?;
    let output = Path::new(&args[1]).canonicalize()?;
    if !output.is_dir() || output.starts_with(&baseline) {
        return Err("output must be a directory outside the baseline directory".into());
    }
    let mode = env::var("POINTLIO_MATH_STUB").unwrap_or_else(|_| "real".into());
    if !["real", "nan", "zero"].contains(&mode.as_str()) {
        return Err("POINTLIO_MATH_STUB must be real, nan or zero".into());
    }
    let selected = env::var("POINTLIO_MATH_COMPONENT").unwrap_or_default();
    if !selected.is_empty() && !COMPONENTS.iter().any(|c| c.name == selected) {
        return Err("unknown POINTLIO_MATH_COMPONENT".into());
    }
    if mode != "real" {
        eprintln!("Red verification: overriding numerical outputs with {mode}");
    }
    if !selected.is_empty() {
        eprintln!(
            "PARTIAL generation: only {selected}; other components contain NaN and are not evaluated"
        );
    }

    // Decode all inputs before writing anything. Baseline output values never
    // participate in candidate calculation; only their schema is read.
    let mut candidates = Vec::with_capacity(COMPONENTS.len());
    for component in COMPONENTS {
        let records = decode(
            &fs::read(baseline.join(format!("{}.bin", component.name)))?,
            component,
        )?;
        if records
            .iter()
            .any(|r| r.input.iter().any(|x| !x.is_finite()))
        {
            return Err(format!("{}: input has NaN/Inf", component.name).into());
        }
        let candidate: Vec<_> = records
            .into_iter()
            .map(|r| {
                let output = if !selected.is_empty() && selected != component.name {
                    vec![f64::NAN; component.output_len]
                } else if mode == "real" {
                    evaluate(component.name, &r.input)
                } else {
                    vec![if mode == "zero" { 0.0 } else { f64::NAN }; component.output_len]
                };
                Record {
                    input: r.input,
                    output,
                }
            })
            .collect();
        let bytes = encode(&candidate, component)?;
        candidates.push((component.name, candidate.len(), bytes));
    }
    for (name, count, bytes) in candidates {
        let mut file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(output.join(format!("{name}.bin")))?;
        file.write_all(&bytes)?;
        println!("wrote {name}.bin records={count}");
    }
    Ok(())
}

fn vec3(v: &[f64], offset: usize) -> [f64; 3] {
    v[offset..offset + 3].try_into().unwrap()
}

fn mat3(v: &[f64], offset: usize) -> Mat3 {
    std::array::from_fn(|r| std::array::from_fn(|c| v[offset + 3 * r + c]))
}

fn mat30(v: &[f64], offset: usize) -> Mat30 {
    std::array::from_fn(|r| std::array::from_fn(|c| v[offset + 30 * r + c]))
}

fn state(v: &[f64], o: usize) -> StateOutput {
    StateOutput {
        pos: vec3(v, o),
        rot: mat3(v, o + 3),
        offset_r_l_i: mat3(v, o + 12),
        offset_t_l_i: vec3(v, o + 21),
        vel: vec3(v, o + 24),
        omg: vec3(v, o + 27),
        acc: vec3(v, o + 30),
        gravity: vec3(v, o + 33),
        bg: vec3(v, o + 36),
        ba: vec3(v, o + 39),
    }
}

fn flatten<const N: usize>(matrix: &[[f64; N]; N]) -> Vec<f64> {
    matrix.iter().flatten().copied().collect()
}

fn state_values(s: &StateOutput) -> Vec<f64> {
    let mut out = Vec::with_capacity(42);
    out.extend(s.pos);
    out.extend(flatten(&s.rot));
    out.extend(flatten(&s.offset_r_l_i));
    for v in [
        &s.offset_t_l_i,
        &s.vel,
        &s.omg,
        &s.acc,
        &s.gravity,
        &s.bg,
        &s.ba,
    ] {
        out.extend(v);
    }
    out
}

fn evaluate(name: &str, input: &[f64]) -> Vec<f64> {
    match name {
        "so3_exp" => flatten(&so3::exp(&vec3(input, 0), 1.0)),
        "so3_log" => so3::log(&mat3(input, 0)).to_vec(),
        "so3_boxplus" => {
            let mut r = mat3(input, 0);
            so3::boxplus(&mut r, &vec3(input, 9), 1.0);
            flatten(&r)
        }
        "so3_boxminus" => so3::boxminus(&mat3(input, 0), &mat3(input, 9)).to_vec(),
        "a_matrix" => flatten(&so3::a_matrix(&vec3(input, 0))),
        "so3math_exp3" => flatten(&so3::exp3(input[0], input[1], input[2])),
        "quat_from_matrix" => so3::quat_from_matrix(&mat3(input, 0)).to_vec(),
        "state_boxplus" => {
            let mut s = state(input, 0);
            s.boxplus(input[42..72].try_into().unwrap(), 1.0);
            state_values(&s)
        }
        "state_boxminus" => state(input, 0).boxminus(&state(input, 42)).to_vec(),
        "f_output" => process::f_output(&state(input, 0)).to_vec(),
        "df_dx_output" => flatten(&process::df_dx_output(&state(input, 0))),
        "predict_state" => {
            let mut s = state(input, 0);
            let mut p = [[0.0; 30]; 30];
            esekf::predict(&mut s, &mut p, &[[0.0; 30]; 30], input[42], true, false);
            state_values(&s)
        }
        "predict_cov" => {
            let mut s = state(input, 0);
            let mut p = mat30(input, 42);
            esekf::predict(&mut s, &mut p, &mat30(input, 942), input[1842], false, true);
            flatten(&p)
        }
        _ => unreachable!("component schema and dispatcher must agree"),
    }
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("ERROR: {error}");
            ExitCode::from(2)
        }
    }
}
