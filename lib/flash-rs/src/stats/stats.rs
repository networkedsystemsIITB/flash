use std::{cell::UnsafeCell, mem};

use crate::{
    config::XdpFlags,
    fd::{FdError, SocketFd},
};

use super::sub::{AppStats, Interface, RingStats, XdpStats};

#[derive(Debug)]
pub struct Stats {
    fd: SocketFd,
    pub interface: Interface,
    pub xdp_flags: XdpFlags,
    pub(crate) ring: UnsafeCell<RingStats>,
    pub(crate) app: UnsafeCell<AppStats>,
}

unsafe impl Send for Stats {}
unsafe impl Sync for Stats {}

impl Stats {
    pub(crate) fn new(fd: SocketFd, ifname: String, ifqueue: u32, xdp_flags: XdpFlags) -> Self {
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

    #[inline]
    pub fn get_ring_stats(&self) -> RingStats {
        unsafe { *self.ring.get() }
    }

    #[inline]
    pub fn get_app_stats(&self) -> AppStats {
        unsafe { *self.app.get() }
    }

    #[inline]
    #[allow(clippy::missing_errors_doc, clippy::missing_transmute_annotations)]
    pub fn get_xdp_stats(&self) -> Result<XdpStats, FdError> {
        let xdp_stats = self.fd.xdp_statistics()?;
        Ok(unsafe { mem::transmute::<_, XdpStats>(xdp_stats) })
    }
}
