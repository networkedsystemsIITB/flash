use std::{io, net::AddrParseError};

use crate::{config::ConfigError, fd::FdError, uds::UdsError, xsk::SocketError};

pub(crate) type FlashResult<T> = Result<T, FlashError>;

#[derive(Debug, thiserror::Error)]
#[error("flash error: {0}")]
pub enum FlashError {
    IO(#[from] io::Error),
    AddrParse(#[from] AddrParseError),

    Config(#[from] ConfigError),
    Fd(#[from] FdError),
    Socket(#[from] SocketError),
    UDS(#[from] UdsError),
}
