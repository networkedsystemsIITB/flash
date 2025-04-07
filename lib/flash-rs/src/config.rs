use std::{io, time::Duration};

use bitflags::bitflags;
use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;

bitflags! {
    #[derive(Debug)]
    pub(crate) struct BindFlags: u32 {
        const XDP_COPY = 2;
        const XDP_ZEROCOPY = 4;
        const XDP_USE_NEED_WAKEUP = 8;
    }
}

bitflags! {
    #[derive(Debug)]
    pub(crate) struct XdpFlags: u32 {
        const XDP_FLAGS_UPDATE_IF_NOEXIST = 1;
        const XDP_FLAGS_SKB_MODE = 2;
        const XDP_FLAGS_DRV_MODE = 4;
        const XDP_FLAGS_HW_MODE = 8;
    }
}

bitflags! {
    #[derive(Debug)]
    pub(crate) struct Mode: u32 {
        const FLASH_BUSY_POLL = 1;
        const FLASH_POLL = 2;
    }
}

#[derive(Debug)]
pub(crate) struct XskConfig {
    pub(crate) bind_flags: BindFlags,
    pub(crate) _xdp_flags: XdpFlags,
    pub(crate) mode: Mode,
    pub(crate) batch_size: u32,
}

#[derive(Debug)]
pub(crate) struct PollConfig {
    pub(crate) idle_timeout: Duration,
    pub(crate) idle_threshold: u32,
    pub(crate) bp_timeout: Duration,
    pub(crate) bp_threshold: u32,
}

impl PollConfig {
    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_precision_loss,
        clippy::cast_sign_loss
    )]
    pub(crate) fn new(
        idle_timeout: Duration,
        idleness: f32,
        bp_timeout: Duration,
        bp_sense: f32,
        batch_size: u32,
    ) -> io::Result<Self> {
        if idle_timeout == Duration::ZERO {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "idle timeout must be greater than 0",
            ));
        }

        let idle_threshold = (batch_size as f32 * idleness) as u32;
        let bp_threshold = (XSK_RING_PROD__DEFAULT_NUM_DESCS as f32 * bp_sense) as u32;

        Ok(Self {
            idle_timeout,
            idle_threshold,
            bp_timeout,
            bp_threshold,
        })
    }
}
