use crate::{mem::PollOutStatus, uds::UdsClient};

use super::{poll::PollConfig, xsk::XskConfig};

#[derive(Debug)]
pub(crate) struct SocketConfig {
    pub(crate) xsk: XskConfig,
    pub(crate) poll: PollConfig,
    pub(crate) pollout_status: PollOutStatus,
    _uds_client: UdsClient,
}

impl SocketConfig {
    pub(crate) fn new(
        xsk: XskConfig,
        poll: PollConfig,
        pollout_status: PollOutStatus,
        uds_client: UdsClient,
    ) -> Self {
        Self {
            xsk,
            poll,
            pollout_status,
            _uds_client: uds_client,
        }
    }
}
