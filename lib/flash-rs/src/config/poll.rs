use std::time::Duration;

use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;

use super::error::{ConfigError, ConfigResult};

#[derive(Debug, PartialEq)]
pub(crate) enum PollMode {
    Smart,
    Sleep,
    None,
}

#[derive(Debug)]
pub(crate) struct PollConfig {
    pub(crate) mode: PollMode,
    pub(crate) next_not_empty: bool,
    pub(crate) idle_timeout: Duration,
    pub(crate) idle_threshold: u32,
    pub(crate) bp_timeout: Duration,
    pub(crate) bp_threshold: u32,
}

impl PollConfig {
    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_precision_loss,
        clippy::cast_sign_loss,
        clippy::too_many_arguments
    )]
    pub(crate) fn new(
        smart_poll: bool,
        sleep_poll: bool,
        next_not_empty: bool,
        idle_timeout: Duration,
        idleness: f32,
        bp_timeout: Duration,
        bp_sense: f32,
        batch_size: u32,
    ) -> ConfigResult<Self> {
        if idle_timeout == Duration::ZERO {
            return Err(ConfigError::InvalidIdleTimeout);
        }

        let mode = if smart_poll {
            PollMode::Smart
        } else if sleep_poll {
            PollMode::Sleep
        } else {
            PollMode::None
        };

        let idle_threshold = (batch_size as f32 * idleness) as u32;
        let bp_threshold = (XSK_RING_PROD__DEFAULT_NUM_DESCS as f32 * bp_sense) as u32;

        Ok(Self {
            mode,
            next_not_empty,
            idle_timeout,
            idle_threshold,
            bp_timeout,
            bp_threshold,
        })
    }
}
