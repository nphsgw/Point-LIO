//! Read B's inputs and emit candidate results. Plan step 3 uses explicit stubs.
use pointlio_math::golden::{COMPONENTS, Record, decode, encode};
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
    let mode = env::var("POINTLIO_MATH_STUB").unwrap_or_else(|_| "nan".into());
    let value = match mode.as_str() {
        "nan" => f64::NAN,
        "zero" => 0.0,
        _ => return Err("POINTLIO_MATH_STUB must be nan or zero".into()),
    };
    eprintln!("UNIMPLEMENTED numerical components: using {mode} stubs for Red verification");

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
            .map(|r| Record {
                input: r.input,
                output: vec![value; component.output_len],
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

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("ERROR: {error}");
            ExitCode::from(2)
        }
    }
}
