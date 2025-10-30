use std::time::Duration;

use super::FlashConfig;

impl FlashConfig {
    #[allow(clippy::must_use_candidate, clippy::too_many_arguments)]
    pub fn new(
        umem_id: usize,
        nf_id: usize,
        smart_poll: bool,
        sleep_poll: bool,
        idle_timeout: Duration,
        idleness: f32,
        bp_timeout: Duration,
        bp_sense: f32,
    ) -> Self {
        Self {
            umem_id,
            nf_id,
            smart_poll,
            sleep_poll,
            idle_timeout,
            idleness,
            bp_timeout,
            bp_sense,
        }
    }
}
