use std::mem;

use libc::{XDP_PGOFF_TX_RING, size_t, xdp_ring_offset};
use libxdp_sys::{
    XSK_RING_PROD__DEFAULT_NUM_DESCS, xdp_desc, xsk_ring_prod, xsk_ring_prod__needs_wakeup,
    xsk_ring_prod__reserve, xsk_ring_prod__submit, xsk_ring_prod__tx_desc,
};

use crate::{fd::SocketFd, mem::Mmap};

use super::{Prod, error::RingResult};

#[derive(Debug)]
pub(crate) struct TxRing {
    ring: xsk_ring_prod,
    _mmap: Mmap,
}

unsafe impl Send for TxRing {}

impl TxRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &SocketFd, off: &xdp_ring_offset, scale: u32) -> RingResult<Self> {
        let tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS * scale;

        let mmap = fd.mmap(
            off.desc as size_t + tx_size as size_t * mem::size_of::<xdp_desc>(),
            XDP_PGOFF_TX_RING,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off)?;

        Ok(Self {
            ring: xsk_ring_prod {
                cached_prod: unsafe { *prod },
                cached_cons: unsafe { *cons } + tx_size,
                mask: tx_size - 1,
                size: tx_size,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(crate) fn desc(&mut self, idx: u32) -> Option<&mut xdp_desc> {
        unsafe { xsk_ring_prod__tx_desc(&raw mut self.ring, idx).as_mut() }
    }
}

impl Prod for TxRing {
    #[inline]
    fn needs_wakeup(&self) -> bool {
        unsafe { xsk_ring_prod__needs_wakeup(&raw const self.ring) != 0 }
    }

    #[inline]
    fn reserve(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_prod__reserve(&raw mut self.ring, nb, idx) }
    }

    #[inline]
    fn submit(&mut self, nb: u32) {
        unsafe { xsk_ring_prod__submit(&raw mut self.ring, nb) }
    }
}
