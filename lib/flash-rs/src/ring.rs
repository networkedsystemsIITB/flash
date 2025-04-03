use std::{io, mem};

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

use crate::{fd::Fd, mmap::Mmap};

const RX_SIZE: u32 = XSK_RING_CONS__DEFAULT_NUM_DESCS;
const TX_SIZE: u32 = XSK_RING_PROD__DEFAULT_NUM_DESCS;

const FILL_SIZE: u32 = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2;
const COMP_SIZE: u32 = XSK_RING_CONS__DEFAULT_NUM_DESCS;

#[derive(Debug)]
pub(crate) struct FillRing {
    ring: xsk_ring_prod,
    _mmap: Mmap,
}

unsafe impl Send for FillRing {}

impl FillRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &Fd, off: &xdp_ring_offset) -> io::Result<Self> {
        let mmap = fd.mmap(
            off.desc as size_t + FILL_SIZE as size_t * mem::size_of::<u64>(),
            XDP_UMEM_PGOFF_FILL_RING as _,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off);

        Ok(Self {
            ring: xsk_ring_prod {
                cached_prod: 0,
                cached_cons: FILL_SIZE,
                mask: FILL_SIZE - 1,
                size: FILL_SIZE,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(crate) fn needs_wakeup(&self) -> bool {
        unsafe { xsk_ring_prod__needs_wakeup(&self.ring) != 0 }
    }

    #[inline]
    pub(crate) fn reserve(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_prod__reserve(&mut self.ring, nb, idx) }
    }

    #[inline]
    pub(crate) fn submit(&mut self, nb: u32) {
        unsafe { xsk_ring_prod__submit(&mut self.ring, nb) }
    }

    #[inline]
    pub(crate) fn addr(&mut self, idx: u32) -> &mut u64 {
        unsafe {
            xsk_ring_prod__fill_addr(&mut self.ring, idx)
                .as_mut()
                .unwrap()
        }
    }
}

#[derive(Debug)]
pub(crate) struct TxRing {
    ring: xsk_ring_prod,
    _mmap: Mmap,
}

unsafe impl Send for TxRing {}

impl TxRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &Fd, off: &xdp_ring_offset) -> io::Result<Self> {
        let mmap = fd.mmap(
            off.desc as size_t + TX_SIZE as size_t * mem::size_of::<xdp_desc>(),
            XDP_PGOFF_TX_RING,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off);

        Ok(Self {
            ring: xsk_ring_prod {
                cached_prod: unsafe { *prod },
                cached_cons: unsafe { *cons } + TX_SIZE,
                mask: TX_SIZE - 1,
                size: TX_SIZE,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(crate) fn needs_wakeup(&self) -> bool {
        unsafe { xsk_ring_prod__needs_wakeup(&self.ring) != 0 }
    }

    #[inline]
    pub(crate) fn reserve(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_prod__reserve(&mut self.ring, nb, idx) }
    }

    #[inline]
    pub(crate) fn submit(&mut self, nb: u32) {
        unsafe { xsk_ring_prod__submit(&mut self.ring, nb) }
    }

    #[inline]
    pub(crate) fn desc(&mut self, idx: u32) -> &mut xdp_desc {
        unsafe {
            xsk_ring_prod__tx_desc(&mut self.ring, idx)
                .as_mut()
                .unwrap()
        }
    }
}

#[derive(Debug)]
pub(crate) struct CompRing {
    ring: xsk_ring_cons,
    _mmap: Mmap,
}

unsafe impl Send for CompRing {}

impl CompRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &Fd, off: &xdp_ring_offset) -> io::Result<Self> {
        let mmap = fd.mmap(
            off.desc as size_t + COMP_SIZE as size_t * mem::size_of::<u64>(),
            XDP_UMEM_PGOFF_COMPLETION_RING as _,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off);

        Ok(Self {
            ring: xsk_ring_cons {
                cached_prod: 0,
                cached_cons: 0,
                mask: COMP_SIZE - 1,
                size: COMP_SIZE,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(crate) fn peek(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_cons__peek(&mut self.ring, nb, idx) }
    }

    #[inline]
    pub(crate) fn release(&mut self, nb: u32) {
        unsafe { xsk_ring_cons__release(&mut self.ring, nb) }
    }

    #[inline]
    pub(crate) fn addr(&self, idx: u32) -> &u64 {
        unsafe { xsk_ring_cons__comp_addr(&self.ring, idx).as_ref().unwrap() }
    }
}

#[derive(Debug)]
pub(crate) struct RxRing {
    ring: xsk_ring_cons,
    _mmap: Mmap,
}

unsafe impl Send for RxRing {}

impl RxRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &Fd, off: &xdp_ring_offset) -> io::Result<Self> {
        let mmap = fd.mmap(
            off.desc as size_t + RX_SIZE as size_t * mem::size_of::<xdp_desc>(),
            XDP_PGOFF_RX_RING,
        )?;

        let (prod, cons, ring, flags) = mmap.add_offset(off);

        Ok(Self {
            ring: xsk_ring_cons {
                cached_prod: unsafe { *prod },
                cached_cons: unsafe { *cons },
                mask: RX_SIZE - 1,
                size: RX_SIZE,
                producer: prod,
                consumer: cons,
                ring,
                flags,
            },
            _mmap: mmap,
        })
    }

    #[inline]
    pub(crate) fn peek(&mut self, nb: u32, idx: &mut u32) -> u32 {
        unsafe { xsk_ring_cons__peek(&mut self.ring, nb, idx) }
    }

    #[inline]
    pub(crate) fn release(&mut self, nb: u32) {
        unsafe { xsk_ring_cons__release(&mut self.ring, nb) }
    }

    #[inline]
    pub(crate) fn desc(&self, idx: u32) -> &xdp_desc {
        unsafe { xsk_ring_cons__rx_desc(&self.ring, idx).as_ref().unwrap() }
    }
}
