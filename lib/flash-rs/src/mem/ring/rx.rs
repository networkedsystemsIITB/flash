use std::mem;

use libc::{XDP_PGOFF_RX_RING, size_t, xdp_ring_offset};
use libxdp_sys::{
    XSK_RING_CONS__DEFAULT_NUM_DESCS, xdp_desc, xsk_ring_cons, xsk_ring_cons__peek,
    xsk_ring_cons__release, xsk_ring_cons__rx_desc,
};

use crate::{fd::Fd, mem::Mmap};

use super::{Cons, error::RingResult};

#[derive(Debug)]
pub(crate) struct RxRing {
    ring: xsk_ring_cons,
    _mmap: Mmap,
}

unsafe impl Send for RxRing {}

impl RxRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &Fd, off: &xdp_ring_offset, scale: u32) -> RingResult<Self> {
        let rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS * scale;

        let mmap = fd.mmap(
            off.desc as size_t + rx_size as size_t * mem::size_of::<xdp_desc>(),
            XDP_PGOFF_RX_RING,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off)?;

        Ok(Self {
            ring: xsk_ring_cons {
                cached_prod: unsafe { *prod },
                cached_cons: unsafe { *cons },
                mask: rx_size - 1,
                size: rx_size,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(crate) fn desc(&self, idx: u32) -> Option<&xdp_desc> {
        unsafe { xsk_ring_cons__rx_desc(&raw const self.ring, idx).as_ref() }
    }
}

impl Cons for RxRing {
    #[inline]
    fn peek(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_cons__peek(&raw mut self.ring, nb, idx) }
    }

    #[inline]
    fn release(&mut self, nb: u32) {
        unsafe { xsk_ring_cons__release(&raw mut self.ring, nb) }
    }
}
