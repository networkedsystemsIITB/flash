use crate::{
    config::{PollConfig, XskConfig},
    uds::UdsClient,
};

#[derive(Debug)]
pub(crate) struct SocketShared {
    pub(super) xsk_config: XskConfig,
    pub(super) poll_config: Option<PollConfig>,
    pub(super) _uds_client: UdsClient,
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
