use std::{io, sync::Arc};

use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;

use crate::{
    Socket, Stats,
    config::{BindFlags, FlashConfig, Mode, PollConfig, XdpFlags, XskConfig},
    mem::Umem,
    socket::{Fd, SocketShared},
    uds::UdsClient,
};

#[allow(clippy::missing_errors_doc)]
pub fn connect(config: &FlashConfig) -> io::Result<Vec<Socket>> {
    let mut uds_client = UdsClient::new()?;

    let (umem_fd, total_sockets, umem_size, umem_scale) =
        uds_client.get_umem(config.nf_id, config.umem_id)?;

    #[cfg(feature = "tracing")]
    {
        tracing::debug!("UMEM FD: {umem_fd}");
        tracing::debug!("Total Sockets: {total_sockets}");
        tracing::debug!("UMEM Size: {umem_size}");
        tracing::debug!("UMEM Scale: {umem_scale}");
    }

    let umem_offset = uds_client.get_umem_offset()?;

    #[cfg(feature = "tracing")]
    tracing::debug!("UMEM Offset: {umem_offset}");

    let bind_flags = BindFlags::from_bits_retain(uds_client.get_bind_flags()?);

    #[cfg(feature = "tracing")]
    tracing::debug!("Bind Flags: {bind_flags:?}");

    let xdp_flags = XdpFlags::from_bits_retain(uds_client.get_xdp_flags()?);

    #[cfg(feature = "tracing")]
    tracing::debug!("XDP Flags: {xdp_flags:?}");

    let mode = Mode::from_bits_retain(uds_client.get_mode()?);

    #[cfg(feature = "tracing")]
    tracing::debug!("Mode: {mode:?}");

    let poll_timeout = if mode.contains(Mode::FLASH_POLL) {
        uds_client.get_poll_timeout()?
    } else {
        0
    };

    #[cfg(feature = "tracing")]
    tracing::debug!("Poll Timeout: {poll_timeout}");

    let mut fd_ifqueue = Vec::with_capacity(total_sockets);

    for _ in 0..total_sockets {
        let (fd, ifqueue) = uds_client.create_socket()?;
        let fd = Fd::new(fd, poll_timeout)?;

        #[cfg(feature = "tracing")]
        tracing::debug!(
            "Socket: {} :: FD: {fd:?} Ifqueue: {ifqueue}",
            fd_ifqueue.len()
        );

        fd_ifqueue.push((fd, ifqueue));
    }

    let ifname = uds_client.get_ifname()?;

    #[cfg(feature = "tracing")]
    tracing::debug!("Ifname: {ifname}");

    uds_client.set_nonblocking()?;

    let xsk_config = XskConfig::new(bind_flags, mode);
    let poll_config = PollConfig::new(
        config.smart_poll,
        config.idle_timeout,
        config.idleness,
        config.bp_timeout,
        config.bp_sense,
        xsk_config.batch_size,
    )?;

    let data = Arc::new(SocketShared::new(xsk_config, poll_config, uds_client));

    let mut sockets = fd_ifqueue
        .into_iter()
        .map(|(fd, ifqueue)| {
            Socket::new(
                fd.clone(),
                Umem::new(umem_fd, umem_size)?,
                Stats::new(fd, ifname.clone(), ifqueue, xdp_flags.clone()),
                data.clone(),
            )
        })
        .collect::<Result<Vec<_>, _>>()?;

    let nr_frames = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2 * umem_scale;
    for (i, socket) in sockets.iter_mut().enumerate() {
        let _ = socket.populate_fq(nr_frames, i as u64 + umem_offset);
    }

    Ok(sockets)
}
