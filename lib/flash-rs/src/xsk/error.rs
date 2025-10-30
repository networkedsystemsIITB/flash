use std::io;

use crate::{fd::FdError, mem::MemError, uds::UdsError};

pub(super) type SocketResult<T> = Result<T, SocketError>;

#[derive(Debug, thiserror::Error)]
#[error("xsk error: {0}")]
pub enum SocketError {
    IO(#[from] io::Error),
    Mem(#[from] MemError),
    Fd(#[from] FdError),
    Uds(#[from] UdsError),

    #[error("xsk error: size exceeds buffer length")]
    SizeOverflow,
}
