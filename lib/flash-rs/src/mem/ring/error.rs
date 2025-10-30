use crate::mem::MemError;

pub(super) type RingResult<T> = Result<T, RingError>;
pub(super) type RingError = MemError;
