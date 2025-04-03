use std::{mem, ptr, slice};

#[inline]
pub(crate) fn get_errno() -> i32 {
    unsafe { *libc::__errno_location() }
}

#[inline]
pub(crate) fn as_bytes<T>(data: &T) -> &[u8] {
    unsafe { slice::from_raw_parts(ptr::from_ref::<T>(data).cast::<u8>(), mem::size_of::<T>()) }
}
