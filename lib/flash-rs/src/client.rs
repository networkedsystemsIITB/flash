use std::{io, sync::Arc};

use libxdp_sys::XSK_RING_PROD__DEFAULT_NUM_DESCS;
use tracing::info;

use crate::{
    Socket,
    config::{BindFlags, Mode, XdpFlags, XskConfig},
    def::{
        BATCH_SIZE, FLASH_CREATE_SOCKET, FLASH_GET_BIND_FLAGS, FLASH_GET_FRAGS_ENABLED,
        FLASH_GET_IFNAME, FLASH_GET_MODE, FLASH_GET_POLL_TIMEOUT, FLASH_GET_ROUTE_INFO,
        FLASH_GET_UMEM, FLASH_GET_UMEM_OFFSET, FLASH_GET_XDP_FLAGS,
    },
    socket::Data,
    uds_conn::UdsConn,
    umem::Umem,
    util,
};

#[repr(C)]
struct NfData {
    nf_id: u32,
    umem_id: u32,
}

#[allow(clippy::cast_sign_loss, clippy::similar_names, clippy::too_many_lines)]
pub fn connect(nf_id: u32, umem_id: u32) -> io::Result<Vec<Socket>> {
    let mut uds_conn = UdsConn::new()?;

    info!("Sending FLASH_GET_UMEM: {FLASH_GET_UMEM:?}");
    uds_conn.write_all(&FLASH_GET_UMEM)?;
    uds_conn.write_all(util::as_bytes(&NfData { nf_id, umem_id }))?;

    let umem_fd = uds_conn.recv_fd()?;
    if umem_fd < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid UMEM FD: {umem_fd}",
        ));
    }
    info!("UMEM FD: {umem_fd}");

    let total_sockets = uds_conn.recv_i32()?;
    if total_sockets <= 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid Total Sockets: {total_sockets}",
        ));
    }
    let total_sockets = total_sockets as usize;

    let umem_size = uds_conn.recv_i32()?;
    if umem_size <= 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid UMEM Size: {umem_size}",
        ));
    }
    let umem_size = umem_size as usize;

    let umem_scale = uds_conn.recv_i32()?;
    if umem_scale <= 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid UMEM Scale: {umem_scale}",
        ));
    }
    let umem_scale = umem_scale as u32;

    info!("Sending FLASH_GET_UMEM_OFFSET: {FLASH_GET_UMEM_OFFSET:?}");
    uds_conn.write_all(&FLASH_GET_UMEM_OFFSET).unwrap();

    let umem_offset = uds_conn.recv_i32()?;
    if umem_offset < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid UMEM Offset: {umem_offset}",
        ));
    }
    let umem_offset = umem_offset as u64;

    let fd_ifqueue = (0..total_sockets)
        .map(|i| {
            info!(
                "Sending FLASH_CREATE_SOCKET ({}): {FLASH_CREATE_SOCKET:?}",
                i + 1
            );
            uds_conn.write_all(&FLASH_CREATE_SOCKET).unwrap();

            (uds_conn.recv_fd().unwrap(), uds_conn.recv_i32().unwrap())
        })
        .collect::<Vec<_>>();
    info!("(fd, ifqueue): {fd_ifqueue:?}");

    info!("Sending FLASH_GET_ROUTE_INFO: {FLASH_GET_ROUTE_INFO:?}");
    uds_conn.write_all(&FLASH_GET_ROUTE_INFO).unwrap();

    let next_size = uds_conn.recv_i32()?;
    if next_size < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid Next Size: {next_size}",
        ));
    }
    let next_size = next_size as usize;

    let next = (0..next_size)
        .map(|_| uds_conn.recv_i32())
        .collect::<Result<Vec<_>, _>>()?;
    info!("Next: {next:?}");

    info!("Sending FLASH_GET_BIND_FLAGS: {FLASH_GET_BIND_FLAGS:?}");
    uds_conn.write_all(&FLASH_GET_BIND_FLAGS).unwrap();

    let xsk_bind_flags = uds_conn.recv_i32()?;
    if xsk_bind_flags < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid XSK Bind Flags: {xsk_bind_flags}",
        ));
    }

    let xsk_bind_flags = BindFlags::from_bits_retain(xsk_bind_flags as u32);
    info!("XSK Bind Flags: {xsk_bind_flags:?}");

    info!("Sending FLASH_GET_XDP_FLAGS: {FLASH_GET_XDP_FLAGS:?}");
    uds_conn.write_all(&FLASH_GET_XDP_FLAGS).unwrap();

    let xsk_xdp_flags = uds_conn.recv_i32()?;
    if xsk_xdp_flags < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid XSK XDP Flags: {xsk_xdp_flags}",
        ));
    }

    let xsk_xdp_flags = XdpFlags::from_bits_retain(xsk_xdp_flags as u32);
    info!("XSK XDP Flags: {xsk_xdp_flags:?}");

    info!("Sending FLASH_GET_MODE: {FLASH_GET_MODE:?}");
    uds_conn.write_all(&FLASH_GET_MODE).unwrap();

    let xsk_mode = uds_conn.recv_i32()?;
    if xsk_mode < 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Invalid XSK Mode: {xsk_mode}",
        ));
    }

    let xsk_mode = Mode::from_bits_retain(xsk_mode as u32);
    info!("XSK Mode: {xsk_mode:?}");

    let _xsk_poll_timeout = if xsk_mode.contains(Mode::FLASH_POLL) {
        info!("Sending FLASH_GET_POLL_TIMEOUT: {FLASH_GET_POLL_TIMEOUT:?}");
        uds_conn.write_all(&FLASH_GET_POLL_TIMEOUT).unwrap();

        uds_conn.recv_i32()?
    } else {
        0
    };

    info!("Sending FLASH_GET_FRAGS_ENABLED: {FLASH_GET_FRAGS_ENABLED:?}");
    uds_conn.write_all(&FLASH_GET_FRAGS_ENABLED).unwrap();

    let _frags_enabled = uds_conn.recv_bool()?;

    info!("Sending FLASH_GET_IFNAME: {FLASH_GET_IFNAME:?}");
    uds_conn.write_all(&FLASH_GET_IFNAME).unwrap();

    let ifname = uds_conn.recv_string()?;
    info!("Ifname: {ifname}");

    uds_conn.set_nonblocking(true)?;

    let umem = Umem::new(umem_fd, umem_size)?;
    let xsk_config = XskConfig {
        bind_flags: xsk_bind_flags,
        _xdp_flags: xsk_xdp_flags,
        mode: xsk_mode,
        batch_size: BATCH_SIZE,
    };

    let data = Arc::new(Data::new(ifname, xsk_config, umem, uds_conn));

    let mut sockets = fd_ifqueue
        .into_iter()
        .map(|(fd, ifqueue)| Socket::new(fd, ifqueue, data.clone()))
        .collect::<Result<Vec<_>, _>>()
        .unwrap();

    let nr_frames = XSK_RING_PROD__DEFAULT_NUM_DESCS * 2 * umem_scale;
    for (i, socket) in sockets.iter_mut().enumerate() {
        socket
            .populate_fq(nr_frames, i as u64 + umem_offset)
            .unwrap();
    }

    Ok(sockets)
}
