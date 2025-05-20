use std::{
    io,
    ptr::{self, NonNull},
    slice,
};

use libc::{
    _SC_PAGESIZE, MAP_POPULATE, MAP_SHARED, PROT_READ, PROT_WRITE, c_void, xdp_ring_offset,
};
use libxdp_sys::xsk_umem__get_data;

use super::error::{MemError, MemResult};

#[derive(Debug)]
pub(crate) struct Mmap {
    addr: NonNull<c_void>,
    len: usize,
}

unsafe impl Send for Mmap {}

impl Mmap {
    pub(crate) fn new(len: usize, fd: i32, offset: i64, populate: bool) -> MemResult<Self> {
        let addr = unsafe {
            libc::mmap(
                ptr::null_mut(),
                len,
                PROT_READ | PROT_WRITE,
                if populate {
                    MAP_SHARED | MAP_POPULATE
                } else {
                    MAP_SHARED
                },
                fd,
                offset,
            )
        };

        if let Some(addr) = NonNull::new(addr) {
            Ok(Mmap { addr, len })
        } else {
            Err(MemError::last_os_error())
        }
    }

    #[allow(clippy::cast_sign_loss, clippy::cast_possible_truncation)]
    pub(super) fn is_page_aligned(&self) -> bool {
        let addr = self.addr.as_ptr() as usize;
        let pg_size = unsafe { libc::sysconf(_SC_PAGESIZE) as usize };

        addr & (pg_size - 1) == 0
    }

    #[allow(clippy::cast_possible_truncation)]
    pub(crate) fn add_offset(
        &self,
        offset: &xdp_ring_offset,
    ) -> MemResult<(*mut u32, *mut u32, *mut libc::c_void, *mut u32)> {
        let prod = offset.producer as usize;
        let cons = offset.consumer as usize;
        let desc = offset.desc as usize;
        let flags = offset.flags as usize;

        if prod > self.len || cons > self.len || desc > self.len || flags > self.len {
            Err(MemError::MmapOffset)
        } else {
            Ok((
                self.addr.as_ptr().wrapping_add(prod).cast(),
                self.addr.as_ptr().wrapping_add(cons).cast(),
                self.addr.as_ptr().wrapping_add(desc),
                self.addr.as_ptr().wrapping_add(flags).cast(),
            ))
        }
    }

    #[allow(clippy::cast_possible_truncation)]
    #[inline]
    pub(super) fn get_data(&mut self, offset: u64, len: usize) -> MemResult<&mut [u8]> {
        if offset as usize + len > self.len {
            Err(MemError::MmapOffset)
        } else {
            Ok(unsafe {
                slice::from_raw_parts_mut(
                    xsk_umem__get_data(self.addr.as_mut(), offset).cast(),
                    len,
                )
            })
        }
    }
}

impl Drop for Mmap {
    fn drop(&mut self) {
        if unsafe { libc::munmap(self.addr.as_ptr(), self.len) } != 0 {
            #[cfg(feature = "tracing")]
            tracing::error!("error unmapping memory: {}", io::Error::last_os_error());

            #[cfg(not(feature = "tracing"))]
            eprintln!("error unmapping memory: {}", io::Error::last_os_error());
        }
    }
}
