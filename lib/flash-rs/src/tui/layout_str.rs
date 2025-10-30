use std::{fmt, str::FromStr};

use super::layout::GridLayout;

#[derive(Debug, thiserror::Error)]
#[error(
    "error parsing grid layout: '{0}' | expected format: <number><r|row|c|col> (e.g. '3r' or '4col')"
)]
pub struct GridLayoutParseError(String);

impl GridLayoutParseError {
    pub(super) fn new(msg: impl Into<String>) -> Self {
        GridLayoutParseError(msg.into())
    }
}

impl FromStr for GridLayout {
    type Err = GridLayoutParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.trim();
        let split_pos = match s.find(|c: char| !c.is_ascii_digit()) {
            None | Some(0) => {
                return Err(GridLayoutParseError::new(s));
            }
            Some(pos) => pos,
        };

        let (num_str, suffix) = s.split_at(split_pos);

        let Ok(num) = num_str.parse::<usize>() else {
            return Err(GridLayoutParseError::new(s));
        };

        match suffix.trim() {
            "r" | "row" | "rows" => Ok(GridLayout::Rows(num)),
            "c" | "col" | "cols" | "column" | "columns" => Ok(GridLayout::Columns(num)),
            _ => Err(GridLayoutParseError::new(s)),
        }
    }
}

impl fmt::Display for GridLayout {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            GridLayout::Rows(n) => write!(f, "{n}row"),
            GridLayout::Columns(n) => write!(f, "{n}col"),
        }
    }
}
