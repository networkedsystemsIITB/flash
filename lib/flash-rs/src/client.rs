use std::{
    io,
    net::{AddrParseError, Ipv4Addr},
    str::FromStr,
    sync::Arc,
};

use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;

use crate::{
    Socket,
    config::{BindFlags, FlashConfig, Mode, PollConfig, XskConfig},
    mem::Umem,
    socket::{Fd, SocketShared},
    uds::UdsClient,
};

#[cfg(feature = "stats")]
use crate::{config::XdpFlags, socket::Stats};

#[derive(Debug, thiserror::Error)]
#[error("flash error: {0}")]
pub enum FlashError {
    IO(#[from] io::Error),
    AddrParse(#[from] AddrParseError),
}

#[allow(clippy::missing_errors_doc, clippy::too_many_lines)]
pub fn connect(
    config: &FlashConfig,
    get_next: bool,
) -> Result<(Vec<Socket>, Option<Vec<Ipv4Addr>>), FlashError> {
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

    #[cfg(feature = "stats")]
    let xdp_flags = XdpFlags::from_bits_retain(uds_client.get_xdp_flags()?);

    #[cfg(all(feature = "stats", feature = "tracing"))]
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

    let mut socket_info = Vec::with_capacity(total_sockets);

    for _ in 0..total_sockets {
        #[cfg(feature = "stats")]
        let (fd, ifqueue) = uds_client.create_socket()?;

        #[cfg(not(feature = "stats"))]
        let (fd, _) = uds_client.create_socket()?;

        let fd = Fd::new(fd, poll_timeout)?;

        #[cfg(feature = "tracing")]
        {
            #[cfg(feature = "stats")]
            tracing::debug!(
                "Socket: {} :: FD: {fd:?} Ifqueue: {ifqueue}",
                socket_info.len()
            );

            #[cfg(not(feature = "stats"))]
            tracing::debug!("Socket: {} :: FD: {fd:?}", socket_info.len());
        }

        #[cfg(feature = "stats")]
        socket_info.push((fd, ifqueue));

        #[cfg(not(feature = "stats"))]
        socket_info.push(fd);
    }

    #[cfg(feature = "stats")]
    let ifname = uds_client.get_ifname()?;

    #[cfg(all(feature = "stats", feature = "tracing"))]
    tracing::debug!("Ifname: {ifname}");

    let next = if get_next {
        Some(
            uds_client
                .get_dst_ip_addr()?
                .iter()
                .map(|y| Ipv4Addr::from_str(y))
                .collect::<Result<Vec<_>, _>>()?,
        )
    } else {
        None
    };

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

    let socket_shared = Arc::new(SocketShared::new(xsk_config, poll_config, uds_client));

    #[cfg(feature = "stats")]
    let mut sockets = socket_info
        .into_iter()
        .map(|(fd, ifqueue)| {
            Socket::new(
                fd.clone(),
                Umem::new(umem_fd, umem_size)?,
                Stats::new(fd, ifname.clone(), ifqueue, xdp_flags.clone()),
                socket_shared.clone(),
            )
        })
        .collect::<Result<Vec<_>, _>>()?;

    #[cfg(not(feature = "stats"))]
    let mut sockets = socket_info
        .into_iter()
        .map(|fd| {
            Socket::new(
                fd.clone(),
                Umem::new(umem_fd, umem_size)?,
                socket_shared.clone(),
            )
        })
        .collect::<Result<Vec<_>, _>>()?;

    let nr_frames = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2 * umem_scale;
    for (i, socket) in sockets.iter_mut().enumerate() {
        let _ = socket.populate_fq(nr_frames, i as u64 + umem_offset);
    }

    Ok((sockets, next))
}
