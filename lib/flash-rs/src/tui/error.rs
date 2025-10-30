use std::io;

pub(super) type TuiResult<T> = Result<T, TuiError>;

#[derive(Debug, thiserror::Error)]
#[error("tui error: {0}")]
pub enum TuiError {
    IO(#[from] io::Error),

    #[error("tui error: empty stats")]
    EmptyStats,
}
