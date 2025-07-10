use std::io;

pub(super) type FdResult<T> = Result<T, FdError>;

#[derive(Debug, thiserror::Error)]
#[error("fd error: {0}")]
pub enum FdError {
    IO(#[from] io::Error),

    #[error("fd error: optlen does not match struct size")]
    SockOptSize,
}

impl FdError {
    #[inline]
    pub(crate) fn last_os_error() -> Self {
        FdError::IO(io::Error::last_os_error())
    }
}
