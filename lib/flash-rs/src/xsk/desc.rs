#[derive(Debug)]
pub struct Desc {
    pub(super) addr: u64,
    pub(super) len: u32,
    pub(super) options: u32,
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
}
