use std::{io, ptr};

use libc::{MSG_DONTWAIT, SOL_XDP, XDP_MMAP_OFFSETS, pollfd, recvfrom, sendto, ssize_t};

#[cfg(feature = "stats")]
use libc::XDP_STATISTICS;

use crate::mem::Mmap;

use super::xdp::{XDP_MMAP_OFFSETS_SIZEOF, XdpMmapOffsets};

#[cfg(feature = "stats")]
use super::xdp::{XDP_STATISTICS_SIZEOF, XdpStatistics};

#[allow(clippy::struct_field_names)]
#[derive(Clone, Debug)]
pub(crate) struct Fd {
    id: i32,
    poll_fd: pollfd,
    poll_timeout: i32,
}

impl Fd {
    pub(crate) fn new(id: i32, poll_timeout: i32) -> io::Result<Self> {
        if id < 0 {
            Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid file descriptor",
            ))
        } else {
            Ok(Fd {
                id,
                poll_fd: pollfd {
                    fd: id,
                    events: libc::POLLIN,
                    revents: 0,
                },
                poll_timeout,
            })
        }
    }

    #[inline]
    pub(super) fn mmap(&self, len: usize, offset: i64) -> io::Result<Mmap> {
        Mmap::new(len, self.id, offset, true)
    }

    #[inline]
    pub(super) fn kick(&self) -> ssize_t {
        unsafe { sendto(self.id, ptr::null(), 0, MSG_DONTWAIT, ptr::null(), 0) }
    }

    #[inline]
    pub(super) fn wakeup(&self) {
        unsafe {
            recvfrom(
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
    pub(super) fn poll(&mut self) -> io::Result<bool> {
        match unsafe { libc::poll(&mut self.poll_fd, 1, self.poll_timeout) } {
            -1 => Err(io::Error::last_os_error()),
            0 => Ok(false),
            _ => Ok(true),
        }
    }

    pub(super) fn xdp_mmap_offsets(&self) -> io::Result<XdpMmapOffsets> {
        let mut off = XdpMmapOffsets::default();
        let mut optlen = XDP_MMAP_OFFSETS_SIZEOF;

        if unsafe {
            libc::getsockopt(
                self.id,
                SOL_XDP,
                XDP_MMAP_OFFSETS,
                (&raw mut off).cast(),
                &mut optlen,
            )
        } != 0
        {
            Err(io::Error::last_os_error())
        } else if optlen == XDP_MMAP_OFFSETS_SIZEOF {
            Ok(off)
        } else {
            Err(io::Error::other(
                "`optlen` returned from `getsockopt` does not match `xdp_mmap_offsets` struct size",
            ))
        }
    }

    #[cfg(feature = "stats")]
    pub(super) fn xdp_statistics(&self) -> io::Result<XdpStatistics> {
        let mut stats = XdpStatistics::default();
        let mut optlen = XDP_STATISTICS_SIZEOF;

        if unsafe {
            libc::getsockopt(
                self.id,
                SOL_XDP,
                XDP_STATISTICS,
                (&raw mut stats).cast(),
                &mut optlen,
            )
        } != 0
        {
            Err(io::Error::last_os_error())
        } else if optlen == XDP_STATISTICS_SIZEOF {
            Ok(stats)
        } else {
            Err(io::Error::other(
                "`optlen` returned from `getsockopt` does not match `xdp_statistics` struct size",
            ))
        }
    }
}
