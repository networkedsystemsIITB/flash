use libxdp_sys::{xdp_desc, xsk_umem__add_offset_to_addr, xsk_umem__extract_addr};

use super::FRAME_SIZE;

#[derive(Debug)]
pub struct Desc {
    pub(super) addr: u64,
    pub(super) len: u32,
    options: u32,
}

impl From<u64> for Desc {
    #[inline]
    fn from(addr: u64) -> Self {
        Self {
            addr,
            len: FRAME_SIZE,
            options: 0,
        }
    }
}

impl From<&xdp_desc> for Desc {
    #[inline]
    fn from(desc: &xdp_desc) -> Self {
        Self {
            addr: unsafe { xsk_umem__add_offset_to_addr(desc.addr) },
            len: desc.len,
            options: desc.options,
        }
    }
}

impl Desc {
    #[inline]
    pub fn len(&self) -> usize {
        self.len as usize
    }

    #[allow(clippy::cast_possible_truncation)]
    #[inline]
    pub fn set_next(&mut self, idx: usize) {
        self.options = (self.options & 0xFFFF) | ((idx as u32) << 16);
    }

    #[inline]
    pub(crate) fn extract_addr(self) -> u64 {
        unsafe { xsk_umem__extract_addr(self.addr) }
    }

    #[inline]
    pub(crate) fn copy_to(&self, desc: &mut xdp_desc) {
        desc.addr = self.addr;
        desc.len = self.len;
        desc.options = self.options & 0xFFFF_0000;
    }
}
