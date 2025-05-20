use std::time::Duration;

#[derive(Debug)]
pub struct FlashConfig {
    pub(crate) umem_id: u32,
    pub(crate) nf_id: u32,
    pub(crate) smart_poll: bool,
    pub(crate) idle_timeout: Duration,
    pub(crate) idleness: f32,
    pub(crate) bp_timeout: Duration,
    pub(crate) bp_sense: f32,
}
