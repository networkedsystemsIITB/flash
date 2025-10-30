use std::mem;

use libc::{XDP_UMEM_PGOFF_COMPLETION_RING, size_t, xdp_ring_offset};
use libxdp_sys::{
    XSK_RING_CONS__DEFAULT_NUM_DESCS, xsk_ring_cons, xsk_ring_cons__comp_addr, xsk_ring_cons__peek,
    xsk_ring_cons__release,
};

use crate::{fd::SocketFd, mem::Mmap};

use super::{Cons, error::RingResult};

#[derive(Debug)]
pub(crate) struct CompRing {
    ring: xsk_ring_cons,
    _mmap: Mmap,
}

unsafe impl Send for CompRing {}

impl CompRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &SocketFd, off: &xdp_ring_offset, scale: u32) -> RingResult<Self> {
        let comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS * scale;

        let mmap = fd.mmap(
            off.desc as size_t + comp_size as size_t * mem::size_of::<u64>(),
            XDP_UMEM_PGOFF_COMPLETION_RING as _,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off)?;

        Ok(Self {
            ring: xsk_ring_cons {
                cached_prod: 0,
                cached_cons: 0,
                mask: comp_size - 1,
                size: comp_size,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(crate) fn addr(&self, idx: u32) -> Option<&u64> {
        unsafe { xsk_ring_cons__comp_addr(&raw const self.ring, idx).as_ref() }
    }
}

impl Cons for CompRing {
    #[inline]
    fn peek(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_cons__peek(&raw mut self.ring, nb, idx) }
    }

    #[inline]
    fn release(&mut self, nb: u32) {
        unsafe { xsk_ring_cons__release(&raw mut self.ring, nb) }
    }
}
