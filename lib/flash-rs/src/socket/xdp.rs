use std::mem;

use libc::{xdp_mmap_offsets, xdp_ring_offset, xdp_statistics};

#[allow(clippy::cast_possible_truncation)]
pub(super) const XDP_MMAP_OFFSETS_SIZEOF: u32 = mem::size_of::<xdp_mmap_offsets>() as _;
#[allow(clippy::cast_possible_truncation)]
pub(super) const XDP_STATISTICS_SIZEOF: u32 = mem::size_of::<xdp_statistics>() as _;

#[repr(transparent)]
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

#[derive(Debug)]
#[repr(transparent)]
pub struct XdpStatistics(xdp_statistics);

impl Default for XdpStatistics {
    fn default() -> Self {
        Self(xdp_statistics {
            rx_dropped: 0,
            rx_invalid_descs: 0,
            tx_invalid_descs: 0,
            rx_ring_full: 0,
            rx_fill_ring_empty_descs: 0,
            tx_ring_empty_descs: 0,
        })
    }
}
