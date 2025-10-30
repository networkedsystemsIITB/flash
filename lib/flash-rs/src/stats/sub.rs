#[derive(Debug, Clone)]
pub struct Interface {
    pub name: String,
    pub queue: u32,
}

#[derive(Debug, Default, Clone, Copy)]
pub struct RingStats {
    pub rx: u64,
    pub tx: u64,
    pub drop: u64,
}

#[derive(Debug, Default, Clone, Copy)]
pub struct AppStats {
    pub rx_empty_polls: u64,
    pub fill_fail_polls: u64,
    pub tx_copy_sendtos: u64,
    pub tx_wakeup_sendtos: u64,
    pub opt_polls: u64,
    pub backpressure: u64,
}

#[derive(Debug, Default, Clone, Copy)]
pub struct XdpStats {
    pub rx_dropped: u64,
    pub rx_invalid_descs: u64,
    pub tx_invalid_descs: u64,
    pub rx_ring_full: u64,
    pub rx_fill_ring_empty_descs: u64,
    pub tx_ring_empty_descs: u64,
}
