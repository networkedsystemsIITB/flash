use std::{io, slice};

use libxdp_sys::xsk_umem__get_data;

use super::mmap::Mmap;

#[derive(Debug)]
pub(crate) struct Umem {
    mmap: Mmap,
    pub(crate) scale: u32,
    pub(crate) offset: u64,
}

impl Umem {
    pub(crate) fn new(fd: i32, size: usize, scale: u32, offset: u64) -> io::Result<Self> {
        let mmap = Mmap::new(size, fd, 0, false)?;
        if !mmap.is_xsk_page_aligned() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mmap is not xsk page aligned",
            ));
        }

        Ok(Self {
            mmap,
            scale,
            offset,
        })
    }

    #[inline]
    pub(crate) fn get_data(&mut self, addr: u64, len: usize) -> io::Result<&mut [u8]> {
        if len > self.mmap.len {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("len {len} is out of bounds"),
            ));
        }

        Ok(unsafe {
            slice::from_raw_parts_mut(
                xsk_umem__get_data(self.mmap.addr.as_mut(), addr).cast(),
                len,
            )
        })
    }
}
