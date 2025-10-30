use std::{
    ptr::{self, NonNull},
    slice,
};

use libc::c_void;

use super::error::{MemError, MemResult};

#[derive(Debug)]
pub(crate) struct PollOutStatus {
    addr: NonNull<c_void>,
    size: usize,
    status: NonNull<u8>,
    nf_id: usize,
    prev_nf: Vec<usize>,
}

unsafe impl Send for PollOutStatus {}
unsafe impl Sync for PollOutStatus {}

impl PollOutStatus {
    pub(crate) fn new(fd: i32, size: usize, nf_id: usize, prev_nf: Vec<usize>) -> MemResult<Self> {
        let addr = unsafe {
            libc::mmap(
                ptr::null_mut(),
                size,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                fd,
                0,
            )
        };

        let Some(addr) = NonNull::new(addr) else {
            return Err(MemError::last_os_error());
        };

        let status = unsafe { NonNull::new_unchecked(addr.as_ptr().cast::<u8>()) };

        Ok(Self {
            addr,
            size,
            status,
            nf_id,
            prev_nf,
        })
    }

    #[inline]
    pub(crate) fn set(&self, bool: bool) {
        unsafe {
            *self.status.as_ptr().add(self.nf_id) = u8::from(bool);
        }
    }

    #[inline]
    pub(crate) fn any(&self) -> bool {
        let status = unsafe { slice::from_raw_parts(self.status.as_ptr(), self.size) };
        self.prev_nf.iter().any(|&prev_id| status[prev_id] != 0)
    }
}

impl Drop for PollOutStatus {
    fn drop(&mut self) {
        unsafe {
            libc::munmap(self.addr.as_ptr(), self.size);
        }
    }
}
