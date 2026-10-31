//! PLGOLD01 little-endian file format (golden/math/README.md).
use std::io;

#[derive(Clone, Copy, Debug)]
pub struct Component {
    pub name: &'static str,
    pub input_len: usize,
    pub output_len: usize,
}

pub const COMPONENTS: [Component; 13] = [
    Component {
        name: "so3_exp",
        input_len: 3,
        output_len: 9,
    },
    Component {
        name: "so3_log",
        input_len: 9,
        output_len: 3,
    },
    Component {
        name: "so3_boxplus",
        input_len: 12,
        output_len: 9,
    },
    Component {
        name: "so3_boxminus",
        input_len: 18,
        output_len: 3,
    },
    Component {
        name: "a_matrix",
        input_len: 3,
        output_len: 9,
    },
    Component {
        name: "so3math_exp3",
        input_len: 3,
        output_len: 9,
    },
    Component {
        name: "quat_from_matrix",
        input_len: 9,
        output_len: 4,
    },
    Component {
        name: "state_boxplus",
        input_len: 72,
        output_len: 42,
    },
    Component {
        name: "state_boxminus",
        input_len: 84,
        output_len: 30,
    },
    Component {
        name: "f_output",
        input_len: 42,
        output_len: 30,
    },
    Component {
        name: "df_dx_output",
        input_len: 42,
        output_len: 900,
    },
    Component {
        name: "predict_state",
        input_len: 43,
        output_len: 42,
    },
    Component {
        name: "predict_cov",
        input_len: 1843,
        output_len: 900,
    },
];

#[derive(Debug)]
pub struct Record {
    pub input: Vec<f64>,
    pub output: Vec<f64>,
}

fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

fn file_len(count: usize, component: Component) -> io::Result<usize> {
    if count == 0 || component.input_len == 0 || component.output_len == 0 {
        return Err(invalid("empty records or dimensions"));
    }
    component
        .input_len
        .checked_add(component.output_len)
        .and_then(|n| n.checked_mul(8))
        .and_then(|stride| stride.checked_mul(count))
        .and_then(|payload| payload.checked_add(24))
        .ok_or_else(|| invalid("file length overflow"))
}

pub fn decode(bytes: &[u8], component: Component) -> io::Result<Vec<Record>> {
    if bytes.len() < 24 || &bytes[..8] != b"PLGOLD01" {
        return Err(invalid("invalid or truncated PLGOLD01 header"));
    }
    let field = |offset| u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap());
    let count = field(12) as usize;
    if field(8) != 1
        || field(16) as usize != component.input_len
        || field(20) as usize != component.output_len
    {
        return Err(invalid("unsupported version or component dimensions"));
    }
    if bytes.len() != file_len(count, component)? {
        return Err(invalid("record count and file length disagree"));
    }
    let stride = 8 * (component.input_len + component.output_len);
    let values = |part: &[u8]| {
        part.as_chunks::<8>()
            .0
            .iter()
            .map(|b| f64::from_le_bytes(*b))
            .collect()
    };
    Ok(bytes[24..]
        .chunks_exact(stride)
        .map(|record| Record {
            input: values(&record[..8 * component.input_len]),
            output: values(&record[8 * component.input_len..]),
        })
        .collect())
}

pub fn encode(records: &[Record], component: Component) -> io::Result<Vec<u8>> {
    let length = file_len(records.len(), component)?;
    let header = [
        1,
        u32::try_from(records.len()).map_err(|_| invalid("too many records"))?,
        u32::try_from(component.input_len).map_err(|_| invalid("input dimension overflow"))?,
        u32::try_from(component.output_len).map_err(|_| invalid("output dimension overflow"))?,
    ];
    if records
        .iter()
        .any(|r| r.input.len() != component.input_len || r.output.len() != component.output_len)
    {
        return Err(invalid("record dimensions disagree with component"));
    }
    let mut bytes = Vec::with_capacity(length);
    bytes.extend(b"PLGOLD01");
    for value in header {
        bytes.extend(value.to_le_bytes());
    }
    for record in records {
        for value in record.input.iter().chain(&record.output) {
            bytes.extend(value.to_le_bytes());
        }
    }
    Ok(bytes)
}
