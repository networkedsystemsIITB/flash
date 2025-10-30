mod desc;
mod error;
mod mmap;
mod pollout;
mod ring;
mod umem;

#[cfg(feature = "pool")]
mod pool;

pub(crate) use {
    desc::Desc,
    mmap::Mmap,
    pollout::PollOutStatus,
    ring::{CompRing, Cons, FillRing, Prod, RxRing, TxRing},
    umem::Umem,
};

#[cfg(feature = "pool")]
pub(crate) use pool::Pool;

pub use error::MemError;

const FRAME_SIZE: u32 = libxdp_sys::XSK_UMEM__DEFAULT_FRAME_SIZE;
