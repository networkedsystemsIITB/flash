use std::{
    io,
    ptr::{self, NonNull},
};

use libc::{
    _SC_PAGESIZE, MAP_FAILED, MAP_POPULATE, MAP_SHARED, PROT_READ, PROT_WRITE, c_void,
    xdp_ring_offset,
};

#[derive(Debug)]
pub(crate) struct Mmap {
    pub(super) addr: NonNull<c_void>,
    pub(super) len: usize,
}

unsafe impl Send for Mmap {}
unsafe impl Sync for Mmap {}

impl Mmap {
    pub(crate) fn new(len: usize, fd: i32, offset: i64, populate: bool) -> io::Result<Self> {
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

        if addr == MAP_FAILED {
            Err(io::Error::last_os_error())
        } else if let Some(addr) = NonNull::new(addr) {
            Ok(Mmap { addr, len })
        } else {
            Err(io::Error::other("unexpected null pointer from `mmap()`"))
        }
    }

    #[allow(clippy::cast_sign_loss, clippy::cast_possible_truncation)]
    pub(super) fn is_xsk_page_aligned(&self) -> bool {
        let addr = self.addr.as_ptr() as usize;
        let pg_size = unsafe { libc::sysconf(_SC_PAGESIZE) as usize };

        addr & (pg_size - 1) == 0
    }

    #[allow(clippy::cast_possible_truncation)]
    pub(crate) fn add_offset(
        &self,
        of: &xdp_ring_offset,
    ) -> io::Result<(*mut u32, *mut u32, *mut libc::c_void, *mut u32)> {
        let prod = of.producer as usize;
        if prod > self.len {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("producer offset {prod} is out of bounds "),
            ));
        }

        let cons = of.consumer as usize;
        if cons > self.len {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("consumer offset {cons} is out of bounds "),
            ));
        }

        let desc = of.desc as usize;
        if desc > self.len {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("descriptor offset {desc} is out of bounds "),
            ));
        }

        let flags = of.flags as usize;
        if flags > self.len {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("flags offset {flags} is out of bounds"),
            ));
        }

        Ok((
            self.addr.as_ptr().wrapping_add(prod).cast(),
            self.addr.as_ptr().wrapping_add(cons).cast(),
            self.addr.as_ptr().wrapping_add(desc),
            self.addr.as_ptr().wrapping_add(flags).cast(),
        ))
    }
}

impl Drop for Mmap {
    fn drop(&mut self) {
        #[cfg(feature = "tracing")]
        if unsafe { libc::munmap(self.addr.as_ptr(), self.len) } != 0 {
            tracing::error!(
                "`munmap()` failed with error: {}",
                io::Error::last_os_error()
            );
        }

        #[cfg(not(feature = "tracing"))]
        if unsafe { libc::munmap(self.addr.as_ptr(), self.len) } != 0 {
            eprintln!("error unmapping memory: {}", io::Error::last_os_error());
        }
    }
}
