use std::mem;

use libc::{
    XDP_PGOFF_RX_RING, XDP_PGOFF_TX_RING, XDP_UMEM_PGOFF_COMPLETION_RING, XDP_UMEM_PGOFF_FILL_RING,
    size_t, xdp_ring_offset,
};
use libxdp_sys::{
    XSK_RING_CONS__DEFAULT_NUM_DESCS, XSK_RING_PROD__DEFAULT_NUM_DESCS, xdp_desc, xsk_ring_cons,
    xsk_ring_cons__comp_addr, xsk_ring_cons__peek, xsk_ring_cons__release, xsk_ring_cons__rx_desc,
    xsk_ring_prod, xsk_ring_prod__fill_addr, xsk_ring_prod__needs_wakeup, xsk_ring_prod__reserve,
    xsk_ring_prod__submit, xsk_ring_prod__tx_desc,
};

use crate::mem::Mmap;

use super::{error::SocketResult, fd::Fd};

pub(super) trait Prod {
    fn needs_wakeup(&self) -> bool;
    fn reserve(&mut self, nb: u32, idx: &mut u32) -> u32;
    fn submit(&mut self, nb: u32);
}

pub(super) trait Cons {
    fn peek(&mut self, nb: u32, idx: &mut u32) -> u32;
    fn release(&mut self, nb: u32);
}

#[derive(Debug)]
pub(super) struct FillRing {
    ring: xsk_ring_prod,
    _mmap: Mmap,
}

unsafe impl Send for FillRing {}

impl FillRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(super) fn new(fd: &Fd, off: &xdp_ring_offset, umem_scale: u32) -> SocketResult<Self> {
        let fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2 * umem_scale;

        let mmap = fd.mmap(
            off.desc as size_t + fill_size as size_t * mem::size_of::<u64>(),
            XDP_UMEM_PGOFF_FILL_RING as _,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off)?;

        Ok(Self {
            ring: xsk_ring_prod {
                cached_prod: 0,
                cached_cons: fill_size,
                mask: fill_size - 1,
                size: fill_size,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(super) fn addr(&mut self, idx: u32) -> Option<&mut u64> {
        unsafe { xsk_ring_prod__fill_addr(&mut self.ring, idx).as_mut() }
    }
}

impl Prod for FillRing {
    #[inline]
    fn needs_wakeup(&self) -> bool {
        unsafe { xsk_ring_prod__needs_wakeup(&self.ring) != 0 }
    }

    #[inline]
    fn reserve(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_prod__reserve(&mut self.ring, nb, idx) }
    }

    #[inline]
    fn submit(&mut self, nb: u32) {
        unsafe { xsk_ring_prod__submit(&mut self.ring, nb) }
    }
}

#[derive(Debug)]
pub(super) struct TxRing {
    ring: xsk_ring_prod,
    _mmap: Mmap,
}

unsafe impl Send for TxRing {}

impl TxRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(super) fn new(fd: &Fd, off: &xdp_ring_offset, umem_scale: u32) -> SocketResult<Self> {
        let tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS * umem_scale;

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
    pub(super) fn desc(&mut self, idx: u32) -> Option<&mut xdp_desc> {
        unsafe { xsk_ring_prod__tx_desc(&mut self.ring, idx).as_mut() }
    }
}

impl Prod for TxRing {
    #[inline]
    fn needs_wakeup(&self) -> bool {
        unsafe { xsk_ring_prod__needs_wakeup(&self.ring) != 0 }
    }

    #[inline]
    fn reserve(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_prod__reserve(&mut self.ring, nb, idx) }
    }

    #[inline]
    fn submit(&mut self, nb: u32) {
        unsafe { xsk_ring_prod__submit(&mut self.ring, nb) }
    }
}

#[derive(Debug)]
pub(super) struct CompRing {
    ring: xsk_ring_cons,
    _mmap: Mmap,
}

unsafe impl Send for CompRing {}

impl CompRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(super) fn new(fd: &Fd, off: &xdp_ring_offset, umem_scale: u32) -> SocketResult<Self> {
        let comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS * umem_scale;

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
    pub(super) fn addr(&self, idx: u32) -> Option<&u64> {
        unsafe { xsk_ring_cons__comp_addr(&self.ring, idx).as_ref() }
    }
}

impl Cons for CompRing {
    #[inline]
    fn peek(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_cons__peek(&mut self.ring, nb, idx) }
    }

    #[inline]
    fn release(&mut self, nb: u32) {
        unsafe { xsk_ring_cons__release(&mut self.ring, nb) }
    }
}

#[derive(Debug)]
pub(super) struct RxRing {
    ring: xsk_ring_cons,
    _mmap: Mmap,
}

unsafe impl Send for RxRing {}

impl RxRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(super) fn new(fd: &Fd, off: &xdp_ring_offset, umem_scale: u32) -> SocketResult<Self> {
        let rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS * umem_scale;

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
    pub(super) fn desc(&self, idx: u32) -> Option<&xdp_desc> {
        unsafe { xsk_ring_cons__rx_desc(&self.ring, idx).as_ref() }
    }
}

impl Cons for RxRing {
    #[inline]
    fn peek(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_cons__peek(&mut self.ring, nb, idx) }
    }

    #[inline]
    fn release(&mut self, nb: u32) {
        unsafe { xsk_ring_cons__release(&mut self.ring, nb) }
    }
}
