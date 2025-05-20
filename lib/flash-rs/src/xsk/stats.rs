use std::{cell::UnsafeCell, mem};

use crate::config::XdpFlags;

use super::{Fd, error::SocketResult, xdp::XdpStatistics};

#[derive(Debug)]
pub struct Stats {
    fd: Fd,
    pub interface: Interface,
    pub xdp_flags: XdpFlags,
    pub(super) ring: UnsafeCell<RingStats>,
    pub(super) app: UnsafeCell<AppStats>,
}

unsafe impl Send for Stats {}
unsafe impl Sync for Stats {}

impl Stats {
    pub(crate) fn new(fd: Fd, ifname: String, ifqueue: u32, xdp_flags: XdpFlags) -> Self {
        Self {
            fd,
            interface: Interface {
                name: ifname,
                queue: ifqueue,
            },
            xdp_flags,
            ring: UnsafeCell::new(RingStats::default()),
            app: UnsafeCell::new(AppStats::default()),
        }
    }

    pub fn get_ring_stats(&self) -> RingStats {
        unsafe { (*self.ring.get()).clone() }
    }

    pub fn get_app_stats(&self) -> AppStats {
        unsafe { (*self.app.get()).clone() }
    }

    #[allow(clippy::missing_errors_doc)]
    pub fn get_xdp_stats(&self) -> SocketResult<XdpStats> {
        let xdp_stats = self.fd.xdp_statistics()?;
        Ok(unsafe { mem::transmute::<XdpStatistics, XdpStats>(xdp_stats) })
    }
}

#[derive(Debug, Clone)]
pub struct Interface {
    pub name: String,
    pub queue: u32,
}

#[derive(Debug, Default, Clone)]
pub struct RingStats {
    pub rx: u64,
    pub tx: u64,
    pub drop: u64,
}

#[derive(Debug, Default, Clone)]
pub struct AppStats {
    pub rx_empty_polls: u64,
    pub fill_fail_polls: u64,
    pub tx_copy_sendtos: u64,
    pub tx_wakeup_sendtos: u64,
}

#[derive(Debug, Default, Clone)]
pub struct XdpStats {
    pub rx_dropped: u64,
    pub rx_invalid_descs: u64,
    pub tx_invalid_descs: u64,
    pub rx_ring_full: u64,
    pub rx_fill_ring_empty_descs: u64,
    pub tx_ring_empty_descs: u64,
}
