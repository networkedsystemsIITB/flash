use std::mem;

use libc::{XDP_UMEM_PGOFF_FILL_RING, size_t, xdp_ring_offset};
use libxdp_sys::{
    XSK_RING_PROD__DEFAULT_NUM_DESCS, xsk_ring_prod, xsk_ring_prod__fill_addr,
    xsk_ring_prod__needs_wakeup, xsk_ring_prod__reserve, xsk_ring_prod__submit,
};

use crate::{
    fd::SocketFd,
    mem::{FRAME_SIZE, Mmap},
};

use super::{Prod, error::RingError, error::RingResult};

#[derive(Debug)]
pub(crate) struct FillRing {
    ring: xsk_ring_prod,
    _mmap: Mmap,
}

unsafe impl Send for FillRing {}

impl FillRing {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(fd: &SocketFd, off: &xdp_ring_offset, scale: u32) -> RingResult<Self> {
        let fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2 * scale;

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

    pub(crate) fn populate(&mut self, scale: u32, offset: u64) -> RingResult<()> {
        let frame_size = u64::from(FRAME_SIZE);
        let nr_frames = XSK_RING_PROD__DEFAULT_NUM_DESCS * scale;

        let mut idx_fq = 0;
        if self.reserve(nr_frames, &mut idx_fq) != nr_frames {
            return Err(RingError::FqPopulate);
        }

        let mut addr = offset * frame_size;
        for _ in 0..nr_frames {
            if let Some(fill_addr) = self.addr(idx_fq) {
                *fill_addr = addr;
            }

            idx_fq += 1;
            addr += frame_size;
        }

        self.submit(nr_frames);
        Ok(())
    }

    #[inline]
    pub(crate) fn addr(&mut self, idx: u32) -> Option<&mut u64> {
        unsafe { xsk_ring_prod__fill_addr(&raw mut self.ring, idx).as_mut() }
    }

    #[inline]
    pub(crate) fn is_half_full(&mut self) -> bool {
        self.ring.cached_cons - self.ring.cached_prod <= self.ring.size / 2
    }
}

impl Prod for FillRing {
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
