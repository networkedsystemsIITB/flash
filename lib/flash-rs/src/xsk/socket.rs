use std::{io, sync::Arc, thread};

use quanta::{Clock, Instant};

use crate::{
    config::{BindFlags, Mode},
    fd::Fd,
    mem::{CompRing, Cons as _, Desc, FillRing, Prod as _, RxRing, TxRing, Umem},
};

#[cfg(feature = "pool")]
use crate::mem::Pool;

#[cfg(feature = "stats")]
use crate::stats::Stats;

use super::{
    error::{SocketError, SocketResult},
    shared::SocketShared,
};

#[derive(Debug)]
pub struct Socket {
    fd: Fd,
    umem: Umem,
    fill: FillRing,
    comp: CompRing,
    rx: RxRing,
    tx: TxRing,

    #[cfg(feature = "pool")]
    pool: Pool,

    outstanding_tx: u32,
    clock: Clock,
    idle_timestamp: Option<Instant>,
    shared: Arc<SocketShared>,

    #[cfg(feature = "stats")]
    stats: Arc<Stats>,
}

impl Socket {
    pub(crate) fn new(
        fd: Fd,
        umem: Umem,
        idx: usize,
        umem_scale: u32,
        umem_offset: u64,
        #[cfg(feature = "stats")] stats: Stats,
        data: Arc<SocketShared>,
    ) -> SocketResult<Self> {
        let off = fd.xdp_mmap_offsets()?;

        let mut fill = FillRing::new(&fd, off.fr(), umem_scale)?;
        let comp = CompRing::new(&fd, off.cr(), umem_scale)?;
        let rx = RxRing::new(&fd, off.rx(), umem_scale)?;
        let tx = TxRing::new(&fd, off.tx(), umem_scale)?;

        #[cfg(feature = "pool")]
        fill.populate(umem_scale, idx as u64 + umem_offset)?;

        #[cfg(not(feature = "pool"))]
        fill.populate(2 * umem_scale, idx as u64 + umem_offset)?;

        Ok(Self {
            fd,
            umem,
            rx,
            tx,
            comp,
            fill,

            #[cfg(feature = "pool")]
            pool: Pool::new(umem_scale, idx as u64 + umem_offset),

            outstanding_tx: 0,

            clock: Clock::new(),
            idle_timestamp: None,

            shared: data,

            #[cfg(feature = "stats")]
            stats: Arc::new(stats),
        })
    }

    #[allow(clippy::missing_errors_doc)]
    #[inline]
    pub fn poll(&mut self) -> io::Result<bool> {
        if self.shared.xsk_config.mode.contains(Mode::FLASH_POLL) {
            #[cfg(feature = "stats")]
            unsafe {
                (*self.stats.app.get()).opt_polls += 1;
            }
            self.fd.poll()
        } else {
            Ok(true)
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
            let _ = self.fd.kick();
        }

        let num_outstanding = self.outstanding_tx.min(self.shared.xsk_config.batch_size);
        let mut idx_cq = 0;

        let completed = self.comp.peek(num_outstanding, &mut idx_cq);
        if completed == 0 {
            return;
        }

        #[cfg(feature = "pool")]
        for _ in 0..completed {
            if let Some(comp_addr) = self.comp.addr(idx_cq) {
                self.pool.put(*comp_addr);
            }
            idx_cq += 1;
        }

        #[cfg(not(feature = "pool"))]
        {
            let mut idx_fq = 0;
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
                if let Some(fill_addr) = self.fill.addr(idx_fq) {
                    if let Some(comp_addr) = self.comp.addr(idx_cq) {
                        *fill_addr = *comp_addr;
                    }
                }

                idx_fq += 1;
                idx_cq += 1;
            }

            self.fill.submit(completed);
        }

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
                let _ = self.fd.kick();
            }

            if self.tx.reserve(num, &mut idx_tx) == num {
                return idx_tx;
            }

            if let Some(poll_config) = &self.shared.poll_config {
                if self.outstanding_tx >= poll_config.bp_threshold {
                    thread::sleep(poll_config.bp_timeout);

                    #[cfg(feature = "stats")]
                    unsafe {
                        (*self.stats.app.get()).backpressure += 1;
                    }
                }
            }
        }
    }

    #[cfg(feature = "pool")]
    #[inline]
    fn replenish_fq(&mut self, num: u32) {
        let mut idx_fq = self.reserve_fq(num);
        let mut allocated = 0;

        while allocated < num {
            if let Some(addr) = self.pool.get() {
                if let Some(fill_addr) = self.fill.addr(idx_fq) {
                    *fill_addr = addr;
                    allocated += 1;
                } else {
                    self.pool.put(addr);

                    #[cfg(feature = "tracing")]
                    tracing::warn!("xsk: failed to get fill descriptor");
                }

                idx_fq += 1;
            } else {
                self.complete_tx_rx();

                if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                    || self.tx.needs_wakeup()
                {
                    #[cfg(feature = "stats")]
                    unsafe {
                        (*self.stats.app.get()).tx_wakeup_sendtos += 1;
                    }
                    let _ = self.fd.kick();
                }
            }
        }

        self.fill.submit(num);
    }

    #[allow(clippy::missing_errors_doc)]
    pub fn recv(&mut self) -> SocketResult<Vec<Desc>> {
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
            if rcvd >= poll_config.idle_threshold || self.outstanding_tx > 0 {
                self.idle_timestamp = None;
            }
        }

        #[cfg(feature = "tracing")]
        if rcvd > self.shared.xsk_config.batch_size {
            tracing::warn!("xsk: received more descriptors than batch size");
        }

        let descs = (0..rcvd)
            .filter_map(|i| self.rx.desc(idx_rx + i).map(Into::into))
            .collect();

        #[cfg(feature = "pool")]
        self.replenish_fq(rcvd);

        self.rx.release(rcvd);

        #[cfg(feature = "stats")]
        unsafe {
            (*self.stats.ring.get()).rx += u64::from(rcvd);
        }

        Ok(descs)
    }

    #[cfg(feature = "pool")]
    #[allow(clippy::missing_errors_doc)]
    pub fn alloc(&mut self, num: usize) -> SocketResult<Vec<Desc>> {
        if num == 0 {
            return Ok(vec![]);
        }

        let mut descs = Vec::with_capacity(num);
        while descs.len() < num {
            if let Some(addr) = self.pool.get() {
                descs.push(addr.into());
            } else {
                self.complete_tx_rx();

                if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                    || self.tx.needs_wakeup()
                {
                    #[cfg(feature = "stats")]
                    unsafe {
                        (*self.stats.app.get()).tx_wakeup_sendtos += 1;
                    }
                    let _ = self.fd.kick();
                }
            }
        }

        Ok(descs)
    }

    #[allow(clippy::cast_possible_truncation)]
    pub fn send(&mut self, descs: Vec<Desc>) {
        let n = descs.len() as u32;
        if n == 0 {
            return;
        }

        let mut idx_tx = self.reserve_tx(n);
        for desc in descs {
            if let Some(tx_desc) = self.tx.desc(idx_tx) {
                desc.copy_to(tx_desc);
            } else {
                #[cfg(feature = "tracing")]
                tracing::warn!("xsk: failed to get tx descriptor");
            }

            idx_tx += 1;
        }

        self.tx.submit(n);
        self.outstanding_tx += n;

        #[cfg(feature = "pool")]
        self.complete_tx_rx();

        #[cfg(feature = "stats")]
        unsafe {
            (*self.stats.ring.get()).tx += u64::from(n);
        }
    }

    #[allow(clippy::cast_possible_truncation)]
    pub fn drop(&mut self, descs: Vec<Desc>) {
        let n = descs.len() as u32;
        if n == 0 {
            return;
        }

        #[cfg(feature = "pool")]
        self.pool.extend(descs.into_iter().map(Desc::extract_addr));

        #[cfg(not(feature = "pool"))]
        {
            let mut idx_fq = self.reserve_fq(n);
            for desc in descs {
                if let Some(fill_addr) = self.fill.addr(idx_fq) {
                    *fill_addr = desc.extract_addr();
                } else {
                    #[cfg(feature = "tracing")]
                    tracing::warn!("xsk: failed to get fill descriptor");
                }

                idx_fq += 1;
            }
        }

        self.fill.submit(n);

        #[cfg(feature = "stats")]
        unsafe {
            (*self.stats.ring.get()).drop += u64::from(n);
        }
    }

    #[allow(clippy::missing_errors_doc)]
    #[inline]
    pub fn read(&mut self, desc: &Desc) -> SocketResult<&mut [u8]> {
        Ok(self.umem.get_data(desc)?)
    }

    #[allow(clippy::missing_errors_doc)]
    #[inline]
    pub fn read_exact<const SIZE: usize>(&mut self, desc: &Desc) -> SocketResult<&mut [u8; SIZE]> {
        if SIZE > desc.len() {
            Err(SocketError::SizeOverflow)
        } else {
            unsafe {
                let data = self.umem.get_data(desc)?;
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
