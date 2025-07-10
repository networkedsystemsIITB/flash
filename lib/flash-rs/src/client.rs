use std::{net::Ipv4Addr, str::FromStr, sync::Arc};

use crate::{
    FlashError, Socket,
    config::{BindFlags, FlashConfig, Mode, PollConfig, XskConfig},
    fd::Fd,
    mem::Umem,
    uds::UdsClient,
    xsk::SocketShared,
};

#[cfg(feature = "stats")]
use crate::{config::XdpFlags, stats::Stats};

pub struct Route {
    pub ip_addr: Ipv4Addr,
    pub next: Vec<Ipv4Addr>,
}

#[allow(clippy::missing_errors_doc, clippy::too_many_lines)]
pub fn connect(config: &FlashConfig) -> Result<(Vec<Socket>, Route), FlashError> {
    let mut uds_client = UdsClient::new()?;

    let (umem_fd, total_sockets, umem_size, umem_scale) =
        uds_client.get_umem(config.umem_id, config.nf_id)?;

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

    // let poll_timeout = if mode.contains(Mode::FLASH_POLL) {
    //     uds_client.get_poll_timeout()?
    // } else {
    //     0
    // };

    // #[cfg(feature = "tracing")]
    // tracing::debug!("Poll Timeout: {poll_timeout}");

    let mut socket_info = Vec::with_capacity(total_sockets);

    for _ in 0..total_sockets {
        #[cfg(any(feature = "stats", feature = "tracing"))]
        let (fd, ifqueue) = uds_client.create_socket()?;

        #[cfg(not(any(feature = "stats", feature = "tracing")))]
        let (fd, _) = uds_client.create_socket()?;

        #[cfg(feature = "tracing")]
        tracing::debug!(
            "Socket: {} :: FD: {fd:?} Ifqueue: {ifqueue}",
            socket_info.len()
        );

        #[cfg(feature = "stats")]
        socket_info.push((Fd::new(fd), ifqueue));

        #[cfg(not(feature = "stats"))]
        socket_info.push(Fd::new(fd));
    }

    #[cfg(feature = "stats")]
    let ifname = uds_client.get_ifname()?;

    #[cfg(all(feature = "stats", feature = "tracing"))]
    tracing::debug!("Ifname: {ifname}");

    // let route_size = uds_client.get_route_info()?;

    // #[cfg(feature = "tracing")]
    // tracing::debug!("Route Size: {route_size}");

    let route = Route {
        ip_addr: Ipv4Addr::from_str(&uds_client.get_ip_addr()?)?,
        next: uds_client
            .get_dst_ip_addr()?
            .iter()
            .map(|y| Ipv4Addr::from_str(y))
            .collect::<Result<Vec<_>, _>>()?,
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
    let sockets = socket_info
        .into_iter()
        .enumerate()
        .map(|(i, (fd, ifqueue))| {
            Socket::new(
                fd.clone(),
                Umem::new(umem_fd, umem_size)?,
                i,
                umem_scale,
                umem_offset,
                Stats::new(fd, ifname.clone(), ifqueue, xdp_flags.clone()),
                socket_shared.clone(),
            )
        })
        .collect::<Result<Vec<_>, _>>()?;

    #[cfg(not(feature = "stats"))]
    let sockets = socket_info
        .into_iter()
        .enumerate()
        .map(|(i, fd)| {
            Socket::new(
                fd.clone(),
                Umem::new(umem_fd, umem_size)?,
                i,
                umem_scale,
                umem_offset,
                socket_shared.clone(),
            )
        })
        .collect::<Result<Vec<_>, _>>()?;

    Ok((sockets, route))
}
