use super::{
    desc::Desc,
    error::{MemError, MemResult},
    mmap::Mmap,
};

#[derive(Debug)]
pub(crate) struct Umem(Mmap);

impl Umem {
    pub(crate) fn new(fd: i32, size: usize) -> MemResult<Self> {
        let mmap = Mmap::new(size, fd, 0, false)?;

        if mmap.is_page_aligned() {
            Ok(Self(mmap))
        } else {
            Err(MemError::MmapAlign)
        }
    }

    #[inline]
    pub(crate) fn get_data(&mut self, desc: &Desc) -> MemResult<&mut [u8]> {
        self.0.get_data(desc.addr, desc.len as usize)
    }
}
