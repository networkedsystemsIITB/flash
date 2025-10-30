use std::{fmt, io, ptr};

use libc::{
    EAGAIN, EBUSY, ENETDOWN, ENOBUFS, MSG_DONTWAIT, SOL_XDP, XDP_MMAP_OFFSETS, pollfd, ssize_t,
};

#[cfg(feature = "stats")]
use libc::XDP_STATISTICS;

use crate::{
    mem::{MemError, Mmap},
    util,
};

use super::{
    error::{FdError, FdResult},
    xdp::{XDP_MMAP_OFFSETS_SIZEOF, XdpMmapOffsets},
};

#[cfg(feature = "stats")]
use super::xdp::{XDP_STATISTICS_SIZEOF, XdpStatistics};

#[derive(Clone)]
pub(crate) struct Fd {
    id: i32,
    poll_fd: pollfd,
    // poll_timeout: i32,
}

#[allow(clippy::missing_fields_in_debug)]
impl fmt::Debug for Fd {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Fd").field("id", &self.id).finish()
    }
}

impl Fd {
    pub(crate) fn new(id: i32) -> Self {
        assert!(id >= 0, "fd error: invalid file descriptor: {id}");

        Fd {
            id,
            poll_fd: pollfd {
                fd: id,
                events: libc::POLLIN,
                revents: 0,
            },
            // poll_timeout,
        }
    }

    #[inline]
    pub(crate) fn mmap(&self, len: usize, offset: i64) -> Result<Mmap, MemError> {
        Mmap::new(len, self.id, offset, true)
    }

    #[inline]
    pub(crate) fn kick(&self) -> Result<ssize_t, ()> {
        let n = unsafe { libc::sendto(self.id, ptr::null(), 0, MSG_DONTWAIT, ptr::null(), 0) };

        if n >= 0 {
            Ok(n)
        } else {
            match util::get_errno() {
                ENOBUFS | EAGAIN | EBUSY | ENETDOWN => Ok(0),
                _ => Err(()),
            }
        }
    }

    #[inline]
    pub(crate) fn wakeup(&self) {
        unsafe {
            libc::recvfrom(
                self.id,
                ptr::null_mut(),
                0,
                MSG_DONTWAIT,
                ptr::null_mut(),
                ptr::null_mut(),
            );
        }
    }

    #[inline]
    pub(crate) fn poll(&mut self) -> io::Result<bool> {
        match unsafe { libc::poll(&raw mut self.poll_fd, 1, -1) } {
            -1 => Err(io::Error::last_os_error()),
            0 => Ok(false),
            _ => Ok(true),
        }
    }

    pub(crate) fn xdp_mmap_offsets(&self) -> FdResult<XdpMmapOffsets> {
        let mut off = XdpMmapOffsets::default();
        let mut optlen = XDP_MMAP_OFFSETS_SIZEOF;

        if unsafe {
            libc::getsockopt(
                self.id,
                SOL_XDP,
                XDP_MMAP_OFFSETS,
                (&raw mut off).cast(),
                &raw mut optlen,
            )
        } != 0
        {
            Err(FdError::last_os_error())
        } else if optlen == XDP_MMAP_OFFSETS_SIZEOF {
            Ok(off)
        } else {
            Err(FdError::SockOptSize)
        }
    }

    #[cfg(feature = "stats")]
    pub(crate) fn xdp_statistics(&self) -> FdResult<XdpStatistics> {
        let mut stats = XdpStatistics::default();
        let mut optlen = XDP_STATISTICS_SIZEOF;

        if unsafe {
            libc::getsockopt(
                self.id,
                SOL_XDP,
                XDP_STATISTICS,
                (&raw mut stats).cast(),
                &raw mut optlen,
            )
        } != 0
        {
            Err(FdError::last_os_error())
        } else if optlen == XDP_STATISTICS_SIZEOF {
            Ok(stats)
        } else {
            Err(FdError::SockOptSize)
        }
    }
}
