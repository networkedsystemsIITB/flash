use std::io;

use crate::mmap::Mmap;

#[derive(Debug)]
pub(crate) struct Umem {
    _mmap: Mmap,
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

        Ok(Self { _mmap: mmap })
    }
}
