use std::io;

pub(super) type MemResult<T> = Result<T, MemError>;

#[derive(Debug, thiserror::Error)]
#[error("mem error: {0}")]
pub enum MemError {
    IO(#[from] io::Error),

    #[error("mem error: mmap not page aligned")]
    MmapAlign,

    #[error("mem error: mmap offset out of bounds")]
    MmapOffset,

    #[error("mem error: could not populate fq")]
    FqPopulate,
}

impl MemError {
    #[inline]
    pub(crate) fn last_os_error() -> Self {
        MemError::IO(io::Error::last_os_error())
    }
}
