use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;
use ringbuffer::{AllocRingBuffer, RingBuffer};

use super::FRAME_SIZE;

#[derive(Debug)]
pub(crate) struct Pool(AllocRingBuffer<u64>);

impl Pool {
    pub(crate) fn new(scale: u32, offset: u64) -> Self {
        let nr_frames = XSK_RING_PROD__DEFAULT_NUM_DESCS * scale;
        let mut ring_buffer = AllocRingBuffer::new(2 * nr_frames as usize);

        let nr_frames = u64::from(nr_frames);
        let shift = FRAME_SIZE.trailing_zeros();

        ring_buffer.extend(((offset + nr_frames)..(offset + 2 * nr_frames)).map(|x| x << shift));

        Self(ring_buffer)
    }

    #[inline]
    pub(crate) fn get(&mut self) -> Option<u64> {
        self.0.dequeue()
    }

    #[inline]
    pub(crate) fn put(&mut self, addr: u64) {
        self.0.push(addr);
    }

    #[inline]
    pub(crate) fn put_batch(&mut self, iter: impl IntoIterator<Item = u64>) {
        self.0.extend(iter);
    }
}
