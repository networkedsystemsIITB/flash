use super::{
    error::{MemError, MemResult},
    mmap::Mmap,
};

#[derive(Debug)]
pub(crate) struct Umem {
    mmap: Mmap,
    pub(crate) scale: u32,
    pub(crate) offset: u64,
}

impl Umem {
    pub(crate) fn new(fd: i32, size: usize, scale: u32, offset: u64) -> MemResult<Self> {
        let mmap = Mmap::new(size, fd, 0, false)?;

        if mmap.is_page_aligned() {
            Ok(Self {
                mmap,
                scale,
                offset,
            })
        } else {
            Err(MemError::MmapAlign)
        }
    }

    #[inline]
    pub(crate) fn get_data(&mut self, offset: u64, len: usize) -> MemResult<&mut [u8]> {
        self.mmap.get_data(offset, len)
    }
}
