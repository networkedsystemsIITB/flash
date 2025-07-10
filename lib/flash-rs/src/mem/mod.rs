mod desc;
mod error;
mod mmap;
mod ring;
mod umem;

#[cfg(feature = "pool")]
mod pool;

pub(crate) use desc::Desc;
pub(crate) use mmap::Mmap;
pub(crate) use ring::{CompRing, Cons, FillRing, Prod, RxRing, TxRing};
pub(crate) use umem::Umem;

#[cfg(feature = "pool")]
pub(crate) use pool::Pool;

pub use error::MemError;

const FRAME_SIZE: u32 = libxdp_sys::XSK_UMEM__DEFAULT_FRAME_SIZE;
