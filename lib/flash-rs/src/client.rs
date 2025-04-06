use std::{arch::x86_64::_rdtsc, io, sync::Arc};

use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;

use crate::{
    Socket,
    config::{BindFlags, Mode, XdpFlags, XskConfig},
    mem::Umem,
    socket::SocketShared,
    uds_conn::{
        FLASH_CREATE_SOCKET, FLASH_GET_BIND_FLAGS, FLASH_GET_FRAGS_ENABLED, FLASH_GET_IFNAME,
        FLASH_GET_MODE, FLASH_GET_POLL_TIMEOUT, FLASH_GET_ROUTE_INFO, FLASH_GET_UMEM,
        FLASH_GET_UMEM_OFFSET, FLASH_GET_XDP_FLAGS, UdsConn,
    },
    util,
};

const BATCH_SIZE: u32 = 64;

#[repr(C)]
struct NfData {
    nf_id: u32,
    umem_id: u32,
}

#[allow(
    clippy::cast_sign_loss,
    clippy::similar_names,
    clippy::too_many_lines,
    clippy::missing_errors_doc
)]
pub fn connect(
    nf_id: u32,
    umem_id: u32,
    back_pressure: bool,
    fwd_all: bool,
) -> io::Result<Vec<Socket>> {
    let mut uds_conn = UdsConn::new()?;

    unsafe { _rdtsc() };

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_UMEM: {FLASH_GET_UMEM:?}");

    uds_conn.write_all(&FLASH_GET_UMEM)?;
    uds_conn.write_all(util::as_bytes(&NfData { nf_id, umem_id }))?;

    let umem_fd = uds_conn.recv_fd()?;
    if umem_fd < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid umem fd: {umem_fd}"),
        ));
    }

    #[cfg(feature = "tracing")]
    tracing::debug!("UMEM FD: {umem_fd}");

    let total_sockets = uds_conn.recv_i32()?;
    if total_sockets <= 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid total sockets: {total_sockets}"),
        ));
    }
    let total_sockets = total_sockets as usize;

    let umem_size = uds_conn.recv_i32()?;
    if umem_size <= 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid umem size: {umem_size}"),
        ));
    }
    let umem_size = umem_size as usize;

    let umem_scale = uds_conn.recv_i32()?;
    if umem_scale <= 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid umem scale: {umem_scale}"),
        ));
    }
    let umem_scale = umem_scale as u32;

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_UMEM_OFFSET: {FLASH_GET_UMEM_OFFSET:?}");

    uds_conn.write_all(&FLASH_GET_UMEM_OFFSET)?;

    let umem_offset = uds_conn.recv_i32()?;
    if umem_offset < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid umem offset: {umem_offset}"),
        ));
    }
    let umem_offset = umem_offset as u64;

    let mut fd_ifqueue = Vec::with_capacity(total_sockets);

    for _ in 0..total_sockets {
        #[cfg(feature = "tracing")]
        tracing::debug!("Sending FLASH_CREATE_SOCKET: {FLASH_CREATE_SOCKET:?}");

        uds_conn.write_all(&FLASH_CREATE_SOCKET)?;

        let fd = uds_conn.recv_fd()?;
        if fd < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid socket fd: {fd}"),
            ));
        }

        let ifqueue = uds_conn.recv_i32()?;
        if ifqueue < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid socket ifqueue: {ifqueue}"),
            ));
        }

        fd_ifqueue.push((fd, ifqueue));

        #[cfg(feature = "tracing")]
        tracing::debug!(
            "Socket: {} :: FD: {fd}, IFQUEUE: {ifqueue}",
            fd_ifqueue.len()
        );
    }

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_ROUTE_INFO: {FLASH_GET_ROUTE_INFO:?}");

    uds_conn.write_all(&FLASH_GET_ROUTE_INFO)?;

    let next_size = uds_conn.recv_i32()?;
    if next_size < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid next size: {next_size}"),
        ));
    }
    let next_size = next_size as usize;

    let _next = (0..next_size)
        .map(|_| uds_conn.recv_i32())
        .collect::<Result<Vec<_>, _>>()?;

    #[cfg(feature = "tracing")]
    tracing::debug!("Next: {_next:?}");

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_BIND_FLAGS: {FLASH_GET_BIND_FLAGS:?}");

    uds_conn.write_all(&FLASH_GET_BIND_FLAGS)?;

    let xsk_bind_flags = uds_conn.recv_i32()?;
    if xsk_bind_flags < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid xsk bind flags: {xsk_bind_flags}"),
        ));
    }

    let xsk_bind_flags = BindFlags::from_bits_retain(xsk_bind_flags as u32);

    #[cfg(feature = "tracing")]
    tracing::debug!("XSK Bind Flags: {xsk_bind_flags:?}");

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_XDP_FLAGS: {FLASH_GET_XDP_FLAGS:?}");

    uds_conn.write_all(&FLASH_GET_XDP_FLAGS)?;

    let xsk_xdp_flags = uds_conn.recv_i32()?;
    if xsk_xdp_flags < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid xsk xdp flags: {xsk_xdp_flags}"),
        ));
    }

    let xsk_xdp_flags = XdpFlags::from_bits_retain(xsk_xdp_flags as u32);

    #[cfg(feature = "tracing")]
    tracing::debug!("XSK XDP Flags: {xsk_xdp_flags:?}");

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_MODE: {FLASH_GET_MODE:?}");

    uds_conn.write_all(&FLASH_GET_MODE)?;

    let xsk_mode = uds_conn.recv_i32()?;
    if xsk_mode < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("invalid xsk mode: {xsk_mode}"),
        ));
    }

    let xsk_mode = Mode::from_bits_retain(xsk_mode as u32);

    #[cfg(feature = "tracing")]
    tracing::debug!("XSK Mode: {xsk_mode:?}");

    let _xsk_poll_timeout = if xsk_mode.contains(Mode::FLASH_POLL) {
        #[cfg(feature = "tracing")]
        tracing::debug!("Sending FLASH_GET_POLL_TIMEOUT: {FLASH_GET_POLL_TIMEOUT:?}");

        uds_conn.write_all(&FLASH_GET_POLL_TIMEOUT)?;

        uds_conn.recv_i32()?
    } else {
        0
    };

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_FRAGS_ENABLED: {FLASH_GET_FRAGS_ENABLED:?}");

    uds_conn.write_all(&FLASH_GET_FRAGS_ENABLED)?;

    let _frags_enabled = uds_conn.recv_bool()?;

    #[cfg(feature = "tracing")]
    tracing::debug!("Sending FLASH_GET_IFNAME: {FLASH_GET_IFNAME:?}");

    uds_conn.write_all(&FLASH_GET_IFNAME)?;

    let ifname = uds_conn.recv_string()?;

    #[cfg(feature = "tracing")]
    tracing::debug!("Ifname: {ifname}");

    uds_conn.set_nonblocking(true)?;

    let xsk_config = XskConfig {
        bind_flags: xsk_bind_flags,
        _xdp_flags: xsk_xdp_flags,
        mode: xsk_mode,
        batch_size: BATCH_SIZE,
    };

    let data = Arc::new(SocketShared::new(
        ifname,
        xsk_config,
        uds_conn,
        back_pressure,
        fwd_all,
    ));

    let mut sockets = fd_ifqueue
        .into_iter()
        .map(|(fd, ifqueue)| Socket::new(fd, ifqueue, Umem::new(umem_fd, umem_size)?, data.clone()))
        .collect::<Result<Vec<_>, _>>()?;

    let nr_frames = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2 * umem_scale;
    for (i, socket) in sockets.iter_mut().enumerate() {
        let _ = socket.populate_fq(nr_frames, i as u64 + umem_offset);
    }

    Ok(sockets)
}
