//! Byte-level contracts from golden/math/README.md, independent of the encoder.
use pointlio_math::golden::{COMPONENTS, Component, Record, decode, encode};

const SMALL: Component = Component {
    name: "fixture",
    input_len: 2,
    output_len: 2,
};

fn fixture() -> Vec<u8> {
    let mut bytes = b"PLGOLD01".to_vec();
    // Version 1, two records, two inputs, two outputs.
    for value in [1_u32, 2, 2, 2] {
        bytes.extend(value.to_le_bytes());
    }
    // Little-endian binary64 patterns: +1, -0, -2, +0; +0, +2, +1, -0.
    for bits in [
        0x3ff0000000000000_u64,
        0x8000000000000000,
        0xc000000000000000,
        0,
        0,
        0x4000000000000000,
        0x3ff0000000000000,
        0x8000000000000000,
    ] {
        bytes.extend(bits.to_le_bytes());
    }
    bytes
}

#[test]
fn decodes_independent_little_endian_fixture() {
    let records = decode(&fixture(), SMALL).unwrap();
    assert_eq!(records.len(), 2);
    assert_eq!(records[0].input[0], 1.0);
    assert_eq!(records[0].input[1].to_bits(), (-0.0_f64).to_bits());
    assert_eq!(records[0].output, [-2.0, 0.0]);
    assert_eq!(records[1].input, [0.0, 2.0]);
    assert_eq!(records[1].output[1].to_bits(), (-0.0_f64).to_bits());
}

#[test]
fn encodes_independent_little_endian_fixture() {
    let records = [
        Record {
            input: vec![1.0, -0.0],
            output: vec![-2.0, 0.0],
        },
        Record {
            input: vec![0.0, 2.0],
            output: vec![1.0, -0.0],
        },
    ];
    assert_eq!(encode(&records, SMALL).unwrap(), fixture());
}

#[test]
fn rejects_bad_headers_lengths_and_empty_records() {
    let mut invalid = Vec::new();
    invalid.push(Vec::new());
    invalid.push(fixture()[..23].to_vec());
    invalid.push(fixture()[..fixture().len() - 1].to_vec());
    let mut trailing = fixture();
    trailing.push(0);
    invalid.push(trailing);
    for (offset, value) in [
        (0, 0_u32),
        (8, 2),
        (12, 0),
        (12, u32::MAX),
        (16, 3),
        (20, 3),
    ] {
        let mut bytes = fixture();
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        invalid.push(bytes);
    }
    assert_eq!(invalid.len(), 10);
    for bytes in invalid {
        assert!(decode(&bytes, SMALL).is_err());
    }
}

#[test]
fn writer_rejects_empty_or_wrong_shapes() {
    assert!(encode(&[], SMALL).is_err());
    for (input, output) in [(vec![0.0], vec![0.0; 2]), (vec![0.0; 2], vec![0.0])] {
        assert!(encode(&[Record { input, output }], SMALL).is_err());
    }
}

#[test]
fn preserves_nonfinite_output_bits_for_comparator_to_reject() {
    // The transport must allow Red stubs; the Python comparator rejects NaN/Inf.
    let output = vec![f64::from_bits(0x7ff8000000001234), f64::NEG_INFINITY];
    let records = [Record {
        input: vec![0.0; 2],
        output,
    }];
    let bytes = encode(&records, SMALL).unwrap();
    assert_eq!(&bytes[40..48], &0x7ff8000000001234_u64.to_le_bytes());
    let decoded = decode(&bytes, SMALL).unwrap();
    assert_eq!(decoded[0].output[0].to_bits(), 0x7ff8000000001234);
    assert_eq!(decoded[0].output[1], f64::NEG_INFINITY);
}

#[test]
fn covers_all_thirteen_component_shapes() {
    assert_eq!(COMPONENTS.len(), 13);
    let mut names = std::collections::HashSet::new();
    for component in COMPONENTS {
        assert!(names.insert(component.name));
        let records = [Record {
            input: vec![1.0; component.input_len],
            output: vec![-2.0; component.output_len],
        }];
        let bytes = encode(&records, component).unwrap();
        assert_eq!(
            bytes.len(),
            24 + 8 * (component.input_len + component.output_len)
        );
        let decoded = decode(&bytes, component).unwrap();
        assert_eq!(decoded.len(), 1);
        assert_eq!(decoded[0].input, records[0].input);
        assert_eq!(decoded[0].output, records[0].output);
    }
    assert_eq!(names.len(), 13);
}
