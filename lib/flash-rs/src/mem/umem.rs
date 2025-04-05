use std::{io, slice};

use libxdp_sys::xsk_umem__get_data;

use super::mmap::Mmap;

#[derive(Debug, Clone)]
pub(crate) struct Umem {
    mmap: Mmap,
}

impl Umem {
    pub(crate) fn new(fd: i32, size: usize) -> io::Result<Self> {
        let mmap = Mmap::new(size, fd, 0)?; // don't populate
        if !mmap.is_xsk_page_aligned() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mmap is not xsk page aligned",
            ));
        }

        Ok(Self { mmap })
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
