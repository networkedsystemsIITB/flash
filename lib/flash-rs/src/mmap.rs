use std::{io, ptr::NonNull};

use libc::{
    _SC_PAGESIZE, MAP_FAILED, MAP_POPULATE, MAP_SHARED, PROT_READ, PROT_WRITE, xdp_ring_offset,
};

#[derive(Debug)]
pub(crate) struct Mmap {
    addr: NonNull<libc::c_void>,
    len: usize,
}

unsafe impl Send for Mmap {}
unsafe impl Sync for Mmap {}

impl Mmap {
    pub(crate) fn new(len: usize, fd: i32, offset: i64) -> io::Result<Self> {
        let addr = unsafe {
            libc::mmap(
                core::ptr::null_mut(),
                len,
                PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_POPULATE,
                fd,
                offset,
            )
        };

        if addr == MAP_FAILED {
            Err(io::Error::last_os_error())
        } else {
            let addr = NonNull::new(addr).ok_or_else(|| {
                io::Error::new(
                    io::ErrorKind::Other,
                    "Unexpected null pointer from `mmap()`",
                )
            })?;

            Ok(Mmap { addr, len })
        }
    }

    #[allow(clippy::cast_possible_truncation)]
    #[inline]
    pub(crate) fn add_offset(
        &self,
        of: &xdp_ring_offset,
    ) -> (*mut u32, *mut u32, *mut libc::c_void, *mut u32) {
        (
            self.addr.as_ptr().wrapping_add(of.producer as usize).cast(),
            self.addr.as_ptr().wrapping_add(of.consumer as usize).cast(),
            self.addr.as_ptr().wrapping_add(of.desc as usize),
            self.addr.as_ptr().wrapping_add(of.flags as usize).cast(),
        )
    }

    #[allow(clippy::cast_sign_loss, clippy::cast_possible_truncation)]
    pub(crate) fn is_xsk_page_aligned(&self) -> bool {
        let addr = self.addr.as_ptr() as usize;
        let pg_size = unsafe { libc::sysconf(_SC_PAGESIZE) as usize };

        addr & (pg_size - 1) == 0
    }
}

impl Drop for Mmap {
    fn drop(&mut self) {
        debug_assert_eq!(
            unsafe { libc::munmap(self.addr.as_ptr(), self.len) },
            0,
            "`munmap()` failed with error: {}",
            io::Error::last_os_error()
        );
    }
}
