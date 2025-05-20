use std::io;

pub(super) type UdsResult<T> = Result<T, UdsError>;

#[derive(Debug, thiserror::Error)]
pub enum UdsError {
    #[error("uds error: {0}")]
    IO(#[from] io::Error),

    #[error("uds error: invalid bind flags")]
    InvalidBindFlags,

    #[error("uds error: invalid dest ip addr size")]
    InvalidDstIpSize,

    #[error("uds error: invalid mode")]
    InvalidMode,

    #[error("uds error: invalid route size")]
    InvalidRouteSize,

    #[error("uds error: invalid socket ifqueue")]
    InvalidSocketIfqueue,

    #[error("uds error: invalid total sockets")]
    InvalidTotalSockets,

    #[error("uds error: invalid umem fd")]
    InvalidUmemFd,

    #[error("uds error: invalid umem size")]
    InvalidUmemSize,

    #[error("uds error: invalid umem scale")]
    InvalidUmemScale,

    #[error("uds error: invalid umem offset")]
    InvalidUmemOffset,

    #[error("uds error: invalid xdp flags")]
    InvalidXdpFlags,
}
