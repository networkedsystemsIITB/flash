mod error;
mod shared;
mod socket;

pub(crate) use shared::SocketShared;

pub use error::SocketError;
pub use socket::Socket;
