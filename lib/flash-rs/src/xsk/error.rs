use std::io;

use crate::mem::MemError;

pub(super) type SocketResult<T> = Result<T, SocketError>;

#[derive(Debug, thiserror::Error)]
pub enum SocketError {
    #[error("xsk error: {0}")]
    IO(#[from] io::Error),

    #[error("xsk error: {0}")]
    Mem(#[from] MemError),

    #[error("xsk error: invalid file descriptor")]
    InvalidFd,

    #[error("xsk error: optlen does not match struct size")]
    SockOptSize,

    #[error("xsk error: could not populate fq")]
    FqPopulate,

    #[error("xsk error: received more than batch size")]
    BatchOverflow,

    #[error("xsk error: size exceeds buffer length")]
    SizeOverflow,
}

impl SocketError {
    #[inline]
    pub(crate) fn last_os_error() -> Self {
        SocketError::IO(io::Error::last_os_error())
    }
}
