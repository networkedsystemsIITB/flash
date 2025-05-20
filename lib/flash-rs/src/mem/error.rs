use std::io;

pub(super) type MemResult<T> = Result<T, MemError>;

#[derive(Debug, thiserror::Error)]
pub enum MemError {
    #[error("mem error: {0}")]
    IO(#[from] io::Error),

    #[error("mem error: mmap not page aligned")]
    MmapAlign,

    #[error("mem error: mmap offset out of bounds")]
    MmapOffset,
}

impl MemError {
    #[inline]
    pub(crate) fn last_os_error() -> Self {
        MemError::IO(io::Error::last_os_error())
    }
}
