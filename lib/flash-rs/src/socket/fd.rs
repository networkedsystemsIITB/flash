use std::{
    io::{self, ErrorKind},
    mem, ptr,
};

use libc::{
    MSG_DONTWAIT, SOL_XDP, XDP_MMAP_OFFSETS, recvfrom, sendto, ssize_t, xdp_mmap_offsets,
    xdp_ring_offset,
};

use crate::mem::Mmap;

#[allow(clippy::cast_possible_truncation)]
const XDP_MMAP_OFFSETS_SIZEOF: u32 = mem::size_of::<xdp_mmap_offsets>() as _;

#[derive(Clone, Debug)]
pub(super) struct Fd {
    id: i32,
}

impl Fd {
    pub(super) fn new(id: i32) -> io::Result<Self> {
        if id < 0 {
            Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid file descriptor",
            ))
        } else {
            Ok(Fd { id })
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
            Err(io::Error::new(
                ErrorKind::Other,
                "`optlen` returned from `getsockopt` does not match `xdp_mmap_offsets` struct size",
            ))
        }
    }
}

pub(super) struct XdpMmapOffsets(xdp_mmap_offsets);

impl Default for XdpMmapOffsets {
    fn default() -> Self {
        Self(xdp_mmap_offsets {
            rx: new_xdp_ring_offset(),
            tx: new_xdp_ring_offset(),
            fr: new_xdp_ring_offset(),
            cr: new_xdp_ring_offset(),
        })
    }
}

fn new_xdp_ring_offset() -> xdp_ring_offset {
    xdp_ring_offset {
        producer: 0,
        consumer: 0,
        desc: 0,
        flags: 0,
    }
}

impl XdpMmapOffsets {
    #[inline]
    pub(super) fn rx(&self) -> &xdp_ring_offset {
        &self.0.rx
    }

    #[inline]
    pub(super) fn tx(&self) -> &xdp_ring_offset {
        &self.0.tx
    }

    #[inline]
    pub(super) fn fr(&self) -> &xdp_ring_offset {
        &self.0.fr
    }

    #[inline]
    pub(super) fn cr(&self) -> &xdp_ring_offset {
        &self.0.cr
    }
}
