use std::{io, net::AddrParseError};

use crate::{config::ConfigError, uds::UdsError, xsk::SocketError};

#[derive(Debug, thiserror::Error)]
#[error("flash error: {0}")]
pub enum FlashError {
    IO(#[from] io::Error),
    AddrParse(#[from] AddrParseError),

    Config(#[from] ConfigError),
    UDS(#[from] UdsError),
    Socket(#[from] SocketError),
}
