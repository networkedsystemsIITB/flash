mod common;
mod error;
mod poll;
mod xsk;

#[cfg_attr(feature = "clap", path = "config_clap.rs")]
#[allow(clippy::module_inception)]
mod config;

pub(crate) use {
    poll::PollConfig,
    xsk::{BindFlags, Mode, XskConfig},
};

pub use {config::FlashConfig, error::ConfigError};

#[cfg(feature = "stats")]
pub use xsk::XdpFlags;
