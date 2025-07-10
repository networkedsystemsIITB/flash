mod comp;
mod error;
mod fill;
mod rx;
mod tx;

pub(crate) use comp::CompRing;
pub(crate) use fill::FillRing;
pub(crate) use rx::RxRing;
pub(crate) use tx::TxRing;

pub(crate) trait Prod {
    fn needs_wakeup(&self) -> bool;
    fn reserve(&mut self, nb: u32, idx: &mut u32) -> u32;
    fn submit(&mut self, nb: u32);
}

pub(crate) trait Cons {
    fn peek(&mut self, nb: u32, idx: &mut u32) -> u32;
    fn release(&mut self, nb: u32);
}
