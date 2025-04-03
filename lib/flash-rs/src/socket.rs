use std::{io, sync::Arc};

use libc::{EAGAIN, EBUSY, ENETDOWN, ENOBUFS};
use libxdp_sys::{
    XSK_UMEM__DEFAULT_FRAME_SIZE, xsk_umem__add_offset_to_addr, xsk_umem__extract_addr,
};

use crate::{
    config::{BindFlags, Mode, XskConfig},
    fd::Fd,
    ring::{CompRing, FillRing, RxRing, TxRing},
    uds_conn::UdsConn,
    umem::Umem,
    util,
};

const FRAME_SIZE: u64 = XSK_UMEM__DEFAULT_FRAME_SIZE as u64;

#[derive(Debug)]
pub struct Socket {
    fd: Fd,
    _ifqueue: i32,
    rx: RxRing,
    tx: TxRing,
    fill: FillRing,
    comp: CompRing,
    outstanding_tx: u32,
    idx_fq_bp: u32,
    idx_tx_bp: u32,
    data: Arc<Data>,
}

#[derive(Debug)]
pub(crate) struct Data {
    pub(crate) _ifname: String,
    pub(crate) xsk_config: XskConfig,
    pub(crate) _umem: Umem,
    _uds_conn: UdsConn,
}

impl Data {
    pub(crate) fn new(
        ifname: String,
        xsk_config: XskConfig,
        umem: Umem,
        uds_conn: UdsConn,
    ) -> Self {
        Self {
            _ifname: ifname,
            xsk_config,
            _umem: umem,
            _uds_conn: uds_conn,
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
    pub(crate) fn new(fd: i32, ifqueue: i32, data: Arc<Data>) -> io::Result<Self> {
        let fd = Fd::new(fd)?;
        let off = fd.xdp_mmap_offsets()?;

        Ok(Self {
            fd: fd.clone(),
            _ifqueue: ifqueue,
            rx: RxRing::new(&fd, off.rx())?,
            tx: TxRing::new(&fd, off.tx())?,
            comp: CompRing::new(&fd, off.cr())?,
            fill: FillRing::new(&fd, off.fr())?,
            outstanding_tx: 0,
            idx_fq_bp: 0,
            idx_tx_bp: 0,
            data,
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

    fn reserve_fq(&mut self, num: u32) {
        let mut idx_fq = 0;
        let mut ret = self.fill.reserve(num, &mut idx_fq);

        while ret != num {
            if self.data.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL) || self.fill.needs_wakeup()
            {
                self.fd.wakeup();
            }

            ret = self.fill.reserve(num, &mut idx_fq);
        }

        self.idx_fq_bp = idx_fq;
    }

    fn reserve_tx(&mut self, num: u32) {
        let mut idx_tx = 0;
        let mut ret = self.tx.reserve(num, &mut idx_tx);

        while ret != num {
            self.complete_tx_rx_first();

            if self.data.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL) || self.tx.needs_wakeup() {
                self.kick_tx().unwrap();
            }

            ret = self.tx.reserve(num, &mut idx_tx);
        }

        self.idx_tx_bp = idx_tx;
    }

    #[allow(clippy::similar_names)]
    fn complete_tx_rx_first(&mut self) {
        if self.outstanding_tx == 0 {
            return;
        }

        let mut idx_cq = 0;
        let mut idx_fq = 0;

        if self
            .data
            .xsk_config
            .bind_flags
            .contains(BindFlags::XDP_COPY)
        {
            self.kick_tx().unwrap();
        }

        let num_outstanding = self.outstanding_tx.min(self.data.xsk_config.batch_size);
        let completed = self.comp.peek(num_outstanding, &mut idx_cq);

        if completed > 0 {
            let mut ret = self.fill.reserve(completed, &mut idx_fq);
            while ret != completed {
                if self.data.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL)
                    || self.fill.needs_wakeup()
                {
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

    pub fn recv(&mut self, back_pressure: bool, fwd_all: bool) -> Result<Vec<Desc>, String> {
        let mut idx_rx = 0;
        let mut descs = Vec::new();

        self.complete_tx_rx_first();

        let rcvd = self.rx.peek(self.data.xsk_config.batch_size, &mut idx_rx);

        if rcvd == 0 {
            if self.data.xsk_config.mode.contains(Mode::FLASH_BUSY_POLL) || self.fill.needs_wakeup()
            {
                self.fd.wakeup();
            }

            return Ok(descs);
        }

        if back_pressure {
            if fwd_all {
                self.reserve_tx(rcvd);
            } else {
                self.reserve_fq(rcvd);
            }
        } else {
            // unimplemented!("Not implemented");
        }

        if rcvd > self.data.xsk_config.batch_size {
            return Err(format!(
                "batch_size: {} rcvd: {rcvd}",
                self.data.xsk_config.batch_size
            ));
        }

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

        Ok(descs)
    }

    #[allow(clippy::cast_possible_truncation)]
    pub fn send(&mut self, back_pressure: bool, descs: Vec<Desc>) {
        if descs.is_empty() {
            return;
        }

        let n = descs.len() as u32;
        if !back_pressure {
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
    }

    #[allow(clippy::cast_possible_truncation)]
    pub fn drop(&mut self, back_pressure: bool, descs: Vec<Desc>) {
        if descs.is_empty() {
            return;
        }

        let n = descs.len() as u32;
        if !back_pressure {
            self.reserve_fq(n);
        }

        let mut idx_fq = self.idx_fq_bp;

        for desc in descs {
            *self.fill.addr(idx_fq) = unsafe { xsk_umem__extract_addr(desc.addr) };
            idx_fq += 1;
        }

        self.fill.submit(n);
    }
}
