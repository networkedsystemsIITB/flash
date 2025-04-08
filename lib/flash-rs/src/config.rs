use std::{io, time::Duration};

#[cfg(feature = "clap")]
use std::num::ParseIntError;

use bitflags::bitflags;
use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;

#[cfg(feature = "clap")]
use clap::Parser;

#[cfg(feature = "clap")]
#[derive(Debug, Parser)]
pub struct FlashConfig {
    #[arg(short, long, help = "Umem id used to connect to monitor")]
    pub(crate) umem_id: u32,

    #[arg(short = 'f', long, help = "NF id used to connect to monitor")]
    pub(crate) nf_id: u32,

    #[arg(
        short = 'p',
        long,
        default_value_t = false,
        help = "Enable smart polling mode"
    )]
    pub(crate) smart_poll: bool,

    #[arg(
        short = 'i',
        long,
        default_value = "100",
        value_parser = parse_millis,
        help="Idle timeout for smart polling (in millisecs)"
    )]
    pub(crate) idle_timeout: Duration,

    #[arg(
        short = 'I',
        long,
        default_value_t = 0.,
        help = "Idleness for smart polling [0.0 = busy-polling, 1.0 = polling]"
    )]
    pub(crate) idleness: f32,

    #[arg(short = 'b',
        long,
        default_value = "0",
        value_parser = parse_micros,
        help="Sleep duration under backpressure (in microsecs)"
    )]
    pub(crate) bp_timeout: Duration,

    #[arg(
        short = 'B',
        long,
        default_value_t = 0.5,
        help = "Backpressure sensitivity [0.0 = low (0 pkts), 1.0 = high (2048 pkts)]"
    )]
    pub(crate) bp_sense: f32,
}

#[cfg(feature = "clap")]
fn parse_millis(arg: &str) -> Result<Duration, ParseIntError> {
    Ok(Duration::from_millis(arg.parse()?))
}

#[cfg(feature = "clap")]
fn parse_micros(arg: &str) -> Result<Duration, ParseIntError> {
    Ok(Duration::from_micros(arg.parse()?))
}

#[cfg(not(feature = "clap"))]
#[derive(Debug)]
pub struct FlashConfig {
    pub(crate) nf_id: u32,
    pub(crate) umem_id: u32,
    pub(crate) smart_poll: bool,
    pub(crate) idle_timeout: Duration,
    pub(crate) idleness: f32,
    pub(crate) bp_timeout: Duration,
    pub(crate) bp_sense: f32,
}

impl FlashConfig {
    #[allow(clippy::must_use_candidate)]
    pub fn new(
        nf_id: u32,
        umem_id: u32,
        smart_poll: bool,
        idle_timeout: Duration,
        idleness: f32,
        bp_timeout: Duration,
        bp_sense: f32,
    ) -> Self {
        Self {
            umem_id,
            nf_id,
            smart_poll,
            idle_timeout,
            idleness,
            bp_timeout,
            bp_sense,
        }
    }
}

#[derive(Debug)]
pub(crate) struct XskConfig {
    pub(crate) bind_flags: BindFlags,
    pub(crate) mode: Mode,
    pub(crate) batch_size: u32,
}

impl XskConfig {
    pub(crate) fn new(bind_flags: BindFlags, mode: Mode, batch_size: u32) -> Self {
        Self {
            bind_flags,
            mode,
            batch_size,
        }
    }
}

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
    pub(crate) struct Mode: u32 {
        const FLASH_BUSY_POLL = 1;
        const FLASH_POLL = 2;
    }
}

bitflags! {
    #[derive(Debug, Clone)]
    pub struct XdpFlags: u32 {
        const XDP_FLAGS_UPDATE_IF_NOEXIST = 1;
        const XDP_FLAGS_SKB_MODE = 2;
        const XDP_FLAGS_DRV_MODE = 4;
        const XDP_FLAGS_HW_MODE = 8;
    }
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
        smart_poll: bool,
        idle_timeout: Duration,
        idleness: f32,
        bp_timeout: Duration,
        bp_sense: f32,
        batch_size: u32,
    ) -> io::Result<Option<Self>> {
        if !smart_poll {
            return Ok(None);
        }

        if idle_timeout == Duration::ZERO {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "idle timeout must be greater than 0",
            ));
        }

        let idle_threshold = (batch_size as f32 * idleness) as u32;
        let bp_threshold = (XSK_RING_PROD__DEFAULT_NUM_DESCS as f32 * bp_sense) as u32;

        Ok(Some(Self {
            idle_timeout,
            idle_threshold,
            bp_timeout,
            bp_threshold,
        }))
    }
}
