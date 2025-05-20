use bitflags::bitflags;

const BATCH_SIZE: u32 = 64;

#[derive(Debug)]
pub(crate) struct XskConfig {
    pub(crate) bind_flags: BindFlags,
    pub(crate) mode: Mode,
    pub(crate) batch_size: u32,
}

impl XskConfig {
    pub(crate) fn new(bind_flags: BindFlags, mode: Mode) -> Self {
        Self {
            bind_flags,
            mode,
            batch_size: BATCH_SIZE,
        }
    }
}

bitflags! {
    #[derive(Debug)]
    pub(crate) struct BindFlags: u32 {
        const XDP_COPY = 2;
        const XDP_ZEROCOPY = 4;
        const XDP_USE_NEED_WAKEUP = 8;
    }
}

bitflags! {
    #[derive(Debug)]
    pub(crate) struct Mode: u32 {
        const FLASH_BUSY_POLL = 1;
        const FLASH_POLL = 2;
    }
}

#[cfg(feature = "stats")]
bitflags! {
    #[derive(Debug, Clone)]
    pub struct XdpFlags: u32 {
        const XDP_FLAGS_UPDATE_IF_NOEXIST = 1;
        const XDP_FLAGS_SKB_MODE = 2;
        const XDP_FLAGS_DRV_MODE = 4;
        const XDP_FLAGS_HW_MODE = 8;
    }
}
