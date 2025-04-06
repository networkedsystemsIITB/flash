mod fd;
mod ring;
mod stats;
mod xdp;

use std::{io, sync::Arc};

use libc::{EAGAIN, EBUSY, ENETDOWN, ENOBUFS};
use libxdp_sys::{
    XSK_UMEM__DEFAULT_FRAME_SIZE, xsk_umem__add_offset_to_addr, xsk_umem__extract_addr,
};

use crate::{
    config::{BindFlags, Mode, XskConfig},
    mem::Umem,
    uds_conn::UdsConn,
    util,
};

use ring::{CompRing, FillRing, RxRing, TxRing};

pub(crate) use fd::Fd;
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
    idx_fq_bp: u32,
    idx_tx_bp: u32,
    umem: Umem,
    stats: Arc<Stats>,
    shared: Arc<SocketShared>,
}

#[derive(Debug)]
pub(crate) struct SocketShared {
    xsk_config: XskConfig,
    back_pressure: bool,
    fwd_all: bool,
    _uds_conn: UdsConn,
}

impl SocketShared {
    pub(crate) fn new(
        xsk_config: XskConfig,
        uds_conn: UdsConn,
        back_pressure: bool,
        fwd_all: bool,
    ) -> Self {
        Self {
            xsk_config,
            _uds_conn: uds_conn,
            back_pressure,
            fwd_all,
        }
    }
}

#[derive(Debug)]
pub struct Desc {
    addr: u64,
    len: u32,
    options: u32,
}

impl Socket {
    #[allow(clippy::cast_possible_wrap, clippy::cast_possible_truncation)]
    pub(crate) fn new(
        fd: Fd,
        ifname: String,
        ifqueue: u32,
        umem: Umem,
        data: Arc<SocketShared>,
    ) -> io::Result<Self> {
        let off = fd.xdp_mmap_offsets()?;

        Ok(Self {
            fd: fd.clone(),
            rx: RxRing::new(&fd, off.rx())?,
            tx: TxRing::new(&fd, off.tx())?,
            comp: CompRing::new(&fd, off.cr())?,
            fill: FillRing::new(&fd, off.fr())?,
            outstanding_tx: 0,
            idx_fq_bp: 0,
            idx_tx_bp: 0,
            umem,
            stats: Arc::new(Stats::new(fd, ifname, ifqueue)),
            shared: data,
        })
    }

    pub(crate) fn populate_fq(&mut self, nr_frames: u32, offset: u64) -> Result<(), ()> {
        let mut idx_fq = 0;
        if self.fill.reserve(nr_frames, &mut idx_fq) != nr_frames {
            return Err(());
        }

        for i in 0..u64::from(nr_frames) {
            *self.fill.addr(idx_fq) = (offset + i) * FRAME_SIZE;
            idx_fq += 1;
        }

        self.fill.submit(nr_frames);

        Ok(())
    }

    #[inline]
    fn kick_tx(&self) -> Result<(), ()> {
        if self.fd.kick() >= 0 {
            Ok(())
        } else {
            let errno = util::get_errno();
            if errno == ENOBUFS || errno == EAGAIN || errno == EBUSY || errno == ENETDOWN {
                Ok(())
            } else {
                Err(())
            }
        }
    }

    #[inline]
    fn reserve_fq(&mut self, num: u32) {
        let mut idx_fq = 0;
        let mut ret = self.fill.reserve(num, &mut idx_fq);

        while ret != num {
            if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                || self.fill.needs_wakeup()
            {
                unsafe {
                    (*self.stats.app.get()).fill_fail_polls += 1;
                }
                self.fd.wakeup();
            }

            ret = self.fill.reserve(num, &mut idx_fq);
        }

        self.idx_fq_bp = idx_fq;
    }

    #[inline]
    fn reserve_tx(&mut self, num: u32) {
        let mut idx_tx = 0;
        let mut ret = self.tx.reserve(num, &mut idx_tx);

        while ret != num {
            self.complete_tx_rx();

            if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL) || self.tx.needs_wakeup()
            {
                unsafe {
                    (*self.stats.app.get()).tx_wakeup_sendtos += 1;
                }
                self.kick_tx().unwrap();
            }

            ret = self.tx.reserve(num, &mut idx_tx);
        }

        self.idx_tx_bp = idx_tx;
    }

    #[allow(clippy::similar_names)]
    #[inline]
    fn complete_tx_rx(&mut self) {
        if self.outstanding_tx == 0 {
            return;
        }

        let mut idx_cq = 0;
        let mut idx_fq = 0;

        if self
            .shared
            .xsk_config
            .bind_flags
            .contains(BindFlags::XDP_COPY)
        {
            unsafe {
                (*self.stats.app.get()).tx_copy_sendtos += 1;
            }
            self.kick_tx().unwrap();
        }

        let num_outstanding = self.outstanding_tx.min(self.shared.xsk_config.batch_size);
        let completed = self.comp.peek(num_outstanding, &mut idx_cq);

        if completed > 0 {
            let mut ret = self.fill.reserve(completed, &mut idx_fq);
            while ret != completed {
                if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                    || self.fill.needs_wakeup()
                {
                    unsafe {
                        (*self.stats.app.get()).fill_fail_polls += 1;
                    }
                    self.fd.wakeup();
                }

                ret = self.fill.reserve(completed, &mut idx_fq);
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
    }

    #[allow(clippy::missing_errors_doc)]
    pub fn recv(&mut self) -> io::Result<Vec<Desc>> {
        let mut idx_rx = 0;

        self.complete_tx_rx();

        let rcvd = self.rx.peek(self.shared.xsk_config.batch_size, &mut idx_rx);

        if rcvd == 0 {
            if self.shared.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                || self.fill.needs_wakeup()
            {
                unsafe {
                    (*self.stats.app.get()).rx_empty_polls += 1;
                }
                self.fd.wakeup();
            }

            return Ok(vec![]);
        }

        if self.shared.back_pressure {
            if self.shared.fwd_all {
                self.reserve_tx(rcvd);
            } else {
                self.reserve_fq(rcvd);
            }
        } else {
            // unimplemented!("Not implemented");
        }

        if rcvd > self.shared.xsk_config.batch_size {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("rcvd: {rcvd} > batch size",),
            ));
        }

        let mut descs = Vec::with_capacity(rcvd as usize);
        for _ in 0..rcvd {
            let desc = self.rx.desc(idx_rx);
            idx_rx += 1;

            let addr = unsafe { xsk_umem__add_offset_to_addr(desc.addr) };

            descs.push(Desc {
                addr,
                len: desc.len,
                options: desc.options,
            });
        }

        self.rx.release(rcvd);

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
        if !self.shared.back_pressure {
            self.reserve_tx(n);
        }

        let mut idx_tx = self.idx_tx_bp;

        for desc in descs {
            let tx_desc = self.tx.desc(idx_tx);
            idx_tx += 1;

            tx_desc.addr = desc.addr;
            tx_desc.len = desc.len;
            tx_desc.options = desc.options & 0xFFFF_0000;
        }

        self.tx.submit(n);
        self.outstanding_tx += n;

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
        if !self.shared.back_pressure {
            self.reserve_fq(n);
        }

        let mut idx_fq = self.idx_fq_bp;

        for desc in descs {
            *self.fill.addr(idx_fq) = unsafe { xsk_umem__extract_addr(desc.addr) };
            idx_fq += 1;
        }

        self.fill.submit(n);

        unsafe {
            (*self.stats.ring.get()).dx += u64::from(n);
        }
    }

    #[allow(clippy::missing_errors_doc)]
    #[inline]
    pub fn read(&mut self, desc: &Desc) -> io::Result<&mut [u8]> {
        self.umem.get_data(desc.addr, desc.len as usize)
    }

    #[allow(clippy::must_use_candidate)]
    pub fn stats(&self) -> Arc<Stats> {
        self.stats.clone()
    }
}
