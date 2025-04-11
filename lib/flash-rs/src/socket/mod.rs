mod fd;
mod ring;
mod xdp;

#[cfg(feature = "stats")]
mod stats;

use std::{io, sync::Arc, thread};

use libc::{EAGAIN, EBUSY, ENETDOWN, ENOBUFS};
use libxdp_sys::{
    XSK_RING_PROD__DEFAULT_NUM_DESCS, XSK_UMEM__DEFAULT_FRAME_SIZE, xsk_umem__add_offset_to_addr,
    xsk_umem__extract_addr,
};
use quanta::{Clock, Instant};

use crate::{
    client::FlashError,
    config::{BindFlags, Mode, PollConfig, XskConfig},
    mem::Umem,
    uds::UdsClient,
    util,
};

use ring::{CompRing, FillRing, RxRing, TxRing};

pub(crate) use fd::Fd;

#[cfg(feature = "stats")]
pub use stats::Stats;

const FRAME_SIZE: u64 = XSK_UMEM__DEFAULT_FRAME_SIZE as u64;

#[derive(Debug)]
pub struct Socket {
    fd: Fd,
    rx: RxRing,
    tx: TxRing,
    fill: FillRing,
    comp: CompRing,
    outstanding_tx: u32,
    clock: Clock,
    idle_timestamp: Option<Instant>,
    umem: Umem,
    #[cfg(feature = "stats")]
    stats: Arc<Stats>,
    shared: Arc<SocketShared>,
}

#[derive(Debug)]
pub(crate) struct SocketShared {
    xsk_config: XskConfig,
    poll_config: Option<PollConfig>,
    _uds_client: UdsClient,
}

impl SocketShared {
    pub(crate) fn new(
        xsk_config: XskConfig,
        poll_config: Option<PollConfig>,
        uds_client: UdsClient,
    ) -> Self {
        Self {
            xsk_config,
            poll_config,
            _uds_client: uds_client,
        }
    }
}

impl Socket {
    pub(crate) fn new(
        fd: Fd,
        umem: Umem,
        #[cfg(feature = "stats")] stats: Stats,
        data: Arc<SocketShared>,
    ) -> io::Result<Self> {
        let off = fd.xdp_mmap_offsets()?;

        Ok(Self {
            rx: RxRing::new(&fd, off.rx(), umem.scale)?,
            tx: TxRing::new(&fd, off.tx(), umem.scale)?,
            comp: CompRing::new(&fd, off.cr(), umem.scale)?,
            fill: FillRing::new(&fd, off.fr(), umem.scale)?,
            fd,
            outstanding_tx: 0,
            clock: Clock::new(),
            idle_timestamp: None,
            umem,
            #[cfg(feature = "stats")]
            stats: Arc::new(stats),
            shared: data,
        })
    }

    pub(crate) fn populate_fq(&mut self, idx: usize) -> Result<(), FlashError> {
        let nr_frames = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2 * self.umem.scale;
        let offset = idx as u64 + self.umem.offset;

        let mut idx_fq = 0;
        if self.fill.reserve(nr_frames, &mut idx_fq) != nr_frames {
            return Err(FlashError::FqPopulate);
        }

        for i in 0..u64::from(nr_frames) {
            *self.fill.addr(idx_fq) = (offset + i) * FRAME_SIZE;
            idx_fq += 1;
        }

        self.fill.submit(nr_frames);
        Ok(())
    }

    #[allow(clippy::missing_errors_doc)]
    #[inline]
    pub fn poll(&mut self) -> io::Result<bool> {
        if self.shared.xsk_config.mode.contains(Mode::FLASH_POLL) {
            self.fd.poll()
        } else {
            Ok(true)
        }
    }

    #[inline]
    fn kick_tx(&self) -> Result<(), ()> {
        if self.fd.kick() >= 0 {
            Ok(())
        } else {
            match util::get_errno() {
                ENOBUFS | EAGAIN | EBUSY | ENETDOWN => Ok(()),
                _ => Err(()),
            }
        }
    }

    #[allow(clippy::similar_names)]
    #[inline]
    fn complete_tx_rx(&mut self) {
        if self.outstanding_tx == 0 {
            return;
        }

        if self
            .shared
            .xsk_config
            .bind_flags
            .contains(BindFlags::XDP_COPY)
        {
            #[cfg(feature = "stats")]
            unsafe {
                (*self.stats.app.get()).tx_copy_sendtos += 1;
            }
            self.kick_tx().unwrap();
        }

        let num_outstanding = self.outstanding_tx.min(self.shared.xsk_config.batch_size);
        let mut idx_cq = 0;
        let mut idx_fq = 0;

        let completed = self.comp.peek(num_outstanding, &mut idx_cq);
        if completed == 0 {
            return;
        }

        while self.fill.reserve(completed, &mut idx_fq) != completed {
            if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                || self.fill.needs_wakeup()
            {
                #[cfg(feature = "stats")]
                unsafe {
                    (*self.stats.app.get()).fill_fail_polls += 1;
                }
                self.fd.wakeup();
            }
        }

        for _ in 0..completed {
            *self.fill.addr(idx_fq) = *self.comp.addr(idx_cq);
            idx_fq += 1;
            idx_cq += 1;
        }

        self.fill.submit(completed);
        self.comp.release(completed);
        self.outstanding_tx -= completed;
    }

    #[inline]
    fn reserve_fq(&mut self, num: u32) -> u32 {
        let mut idx_fq = 0;

        while self.fill.reserve(num, &mut idx_fq) != num {
            if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                || self.fill.needs_wakeup()
            {
                #[cfg(feature = "stats")]
                unsafe {
                    (*self.stats.app.get()).fill_fail_polls += 1;
                }
                self.fd.wakeup();
            }
        }

        idx_fq
    }

    #[inline]
    fn reserve_tx(&mut self, num: u32) -> u32 {
        let mut idx_tx = 0;

        if self.tx.reserve(num, &mut idx_tx) == num {
            return idx_tx;
        }

        loop {
            self.complete_tx_rx();

            if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL) || self.tx.needs_wakeup()
            {
                #[cfg(feature = "stats")]
                unsafe {
                    (*self.stats.app.get()).tx_wakeup_sendtos += 1;
                }
                self.kick_tx().unwrap();
            }

            if self.tx.reserve(num, &mut idx_tx) == num {
                return idx_tx;
            }

            if let Some(poll_config) = &self.shared.poll_config {
                if self.outstanding_tx >= poll_config.bp_threshold {
                    thread::sleep(poll_config.bp_timeout);
                }
            }
        }
    }

    #[allow(clippy::missing_errors_doc)]
    pub fn recv(&mut self) -> io::Result<Vec<Desc>> {
        self.complete_tx_rx();

        if let Some(poll_config) = &self.shared.poll_config {
            if let Some(idle_timestamp) = self.idle_timestamp {
                if self.clock.now() >= idle_timestamp && !self.fd.poll()? {
                    self.idle_timestamp = self.clock.now().checked_add(poll_config.idle_timeout);

                    return Ok(vec![]);
                }
            }
        }

        let mut idx_rx = 0;
        let rcvd = self.rx.peek(self.shared.xsk_config.batch_size, &mut idx_rx);

        if rcvd == 0 {
            if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                || self.fill.needs_wakeup()
            {
                #[cfg(feature = "stats")]
                unsafe {
                    (*self.stats.app.get()).rx_empty_polls += 1;
                }
                self.fd.wakeup();
            }

            if let Some(poll_config) = &self.shared.poll_config {
                if self.idle_timestamp.is_none() {
                    self.idle_timestamp = self.clock.now().checked_add(poll_config.idle_timeout);
                }
            }

            return Ok(vec![]);
        }

        if let Some(poll_config) = &self.shared.poll_config {
            if rcvd >= poll_config.idle_threshold {
                self.idle_timestamp = None;
            }
        }

        if rcvd > self.shared.xsk_config.batch_size {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("rcvd: {rcvd} > batch size",),
            ));
        }

        let descs = (0..rcvd)
            .map(|_| {
                let desc = self.rx.desc(idx_rx);
                idx_rx += 1;

                Desc {
                    addr: unsafe { xsk_umem__add_offset_to_addr(desc.addr) },
                    len: desc.len,
                    options: desc.options,
                }
            })
            .collect();

        self.rx.release(rcvd);

        #[cfg(feature = "stats")]
        unsafe {
            (*self.stats.ring.get()).rx += u64::from(rcvd);
        }

        Ok(descs)
    }

    #[allow(clippy::cast_possible_truncation)]
    pub fn send(&mut self, descs: Vec<Desc>) {
        if descs.is_empty() {
            return;
        }

        let n = descs.len() as u32;
        let mut idx_tx = self.reserve_tx(n);

        for desc in descs {
            let tx_desc = self.tx.desc(idx_tx);
            idx_tx += 1;

            tx_desc.addr = desc.addr;
            tx_desc.len = desc.len;
            tx_desc.options = desc.options & 0xFFFF_0000;
        }

        self.tx.submit(n);
        self.outstanding_tx += n;

        #[cfg(feature = "stats")]
        unsafe {
            (*self.stats.ring.get()).tx += u64::from(n);
        }
    }

    #[allow(clippy::cast_possible_truncation)]
    pub fn drop(&mut self, descs: Vec<Desc>) {
        if descs.is_empty() {
            return;
        }

        let n = descs.len() as u32;
        let mut idx_fq = self.reserve_fq(n);

        for desc in descs {
            *self.fill.addr(idx_fq) = unsafe { xsk_umem__extract_addr(desc.addr) };
            idx_fq += 1;
        }

        self.fill.submit(n);

        #[cfg(feature = "stats")]
        unsafe {
            (*self.stats.ring.get()).drop += u64::from(n);
        }
    }

    #[allow(clippy::missing_errors_doc)]
    #[inline]
    pub fn read<const SIZE: usize>(&mut self, desc: &Desc) -> io::Result<&mut [u8; SIZE]> {
        if SIZE > desc.len as usize {
            Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("size: {SIZE} > desc length"),
            ))
        } else {
            unsafe {
                let data = self.umem.get_data(desc.addr, desc.len as usize)?;
                Ok(&mut *data.as_mut_ptr().cast::<[u8; SIZE]>())
            }
        }
    }

    #[cfg(feature = "stats")]
    #[allow(clippy::must_use_candidate)]
    pub fn stats(&self) -> Arc<Stats> {
        self.stats.clone()
    }
}

#[derive(Debug)]
pub struct Desc {
    addr: u64,
    len: u32,
    options: u32,
}

impl Desc {
    #[inline]
    pub fn len(&self) -> usize {
        self.len as usize
    }

    #[allow(clippy::cast_possible_truncation)]
    #[inline]
    pub fn set_next(&mut self, idx: usize) {
        self.options = (self.options & 0xFFFF) | ((idx as u32) << 16);
    }
}
