use std::io;

pub(super) type UdsResult<T> = Result<T, UdsError>;

#[derive(Debug, thiserror::Error)]
#[error("uds error: {0}")]
pub enum UdsError {
    IO(#[from] io::Error),

    #[error("uds error: invalid bind flags")]
    InvalidBindFlags,

    #[error("uds error: invalid mode")]
    InvalidMode,

    #[error("uds error: invalid next size")]
    InvalidNextSize,

    #[error("uds error: invalid socket fd")]
    InvalidSocketFd,

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

    #[error("uds error: invalid pollout fd")]
    InvalidPollOutFd,

    #[error("uds error: invalid pollout size")]
    InvalidPollOutSize,

    #[error("uds error: invalid prev size")]
    InvalidPrevSize,

    #[error("uds error: invalid prev nf id")]
    InvalidPrevNfId,
}
