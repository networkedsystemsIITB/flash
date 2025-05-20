mod common;
mod error;
mod poll;
mod xsk;

#[cfg(feature = "clap")]
mod config_clap;

#[cfg(not(feature = "clap"))]
mod config_noclap;

pub(crate) use poll::PollConfig;
pub(crate) use xsk::{BindFlags, Mode, XskConfig};

pub use error::ConfigError;

#[cfg(feature = "clap")]
pub use config_clap::FlashConfig;

#[cfg(not(feature = "clap"))]
pub use config_noclap::FlashConfig;

#[cfg(feature = "stats")]
pub use xsk::XdpFlags;
