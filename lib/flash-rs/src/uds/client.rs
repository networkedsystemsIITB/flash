use std::io;

use crate::util;

use super::{
    conn::UdsConn,
    def::{
        FLASH_CLOSE_CONN, FLASH_CREATE_SOCKET, FLASH_GET_BIND_FLAGS, FLASH_GET_DST_IP_ADDR,
        FLASH_GET_FRAGS_ENABLED, FLASH_GET_IFNAME, FLASH_GET_IP_ADDR, FLASH_GET_MODE,
        FLASH_GET_POLL_TIMEOUT, FLASH_GET_ROUTE_INFO, FLASH_GET_UMEM, FLASH_GET_UMEM_OFFSET,
        FLASH_GET_XDP_FLAGS,
    },
};

const FLASH_UNIX_SOCKET_PATH: &str = "/tmp/flash/uds.sock";

#[derive(Debug)]
pub(crate) struct UdsClient {
    conn: UdsConn,
}

#[allow(dead_code, clippy::cast_sign_loss)]
impl UdsClient {
    pub(crate) fn new() -> io::Result<Self> {
        Ok(Self {
            conn: UdsConn::new(FLASH_UNIX_SOCKET_PATH)?,
        })
    }

    #[allow(clippy::similar_names)]
    pub(crate) fn get_umem(
        &mut self,
        umem_id: u32,
        nf_id: u32,
    ) -> io::Result<(i32, usize, usize, u32)> {
        #[repr(C)]
        struct NfData {
            umem_id: u32,
            nf_id: u32,
        }

        self.conn.write_all(&FLASH_GET_UMEM)?;
        self.conn
            .write_all(util::as_bytes(&NfData { umem_id, nf_id }))?;

        let umem_fd = self.conn.recv_fd()?;
        if umem_fd < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid umem fd: {umem_fd}"),
            ));
        }

        let total_sockets = self.conn.recv_i32()?;
        if total_sockets <= 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid total sockets: {total_sockets}"),
            ));
        }

        let umem_size = self.conn.recv_i32()?;
        if umem_size <= 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid umem size: {umem_size}"),
            ));
        }

        let umem_scale = self.conn.recv_i32()?;
        if umem_scale <= 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid umem scale: {umem_scale}"),
            ));
        }

        Ok((
            umem_fd,
            total_sockets as usize,
            umem_size as usize,
            umem_scale as u32,
        ))
    }

    pub(crate) fn create_socket(&mut self) -> io::Result<(i32, u32)> {
        self.conn.write_all(&FLASH_CREATE_SOCKET)?;

        let fd = self.conn.recv_fd()?;

        let ifqueue = self.conn.recv_i32()?;
        if ifqueue < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid socket ifqueue: {ifqueue}"),
            ));
        }

        Ok((fd, ifqueue as u32))
    }

    pub(crate) fn get_umem_offset(&mut self) -> io::Result<u64> {
        self.conn.write_all(&FLASH_GET_UMEM_OFFSET)?;

        let umem_offset = self.conn.recv_i32()?;
        if umem_offset < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid umem offset: {umem_offset}"),
            ));
        }

        Ok(umem_offset as u64)
    }

    pub(crate) fn get_route_info(&mut self) -> io::Result<Vec<i32>> {
        self.conn.write_all(&FLASH_GET_ROUTE_INFO)?;

        let next_size = self.conn.recv_i32()?;
        if next_size < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid next size: {next_size}"),
            ));
        }

        (0..next_size as usize)
            .map(|_| self.conn.recv_i32())
            .collect::<Result<Vec<_>, _>>()
    }

    pub(crate) fn get_bind_flags(&mut self) -> io::Result<u32> {
        self.conn.write_all(&FLASH_GET_BIND_FLAGS)?;

        let bind_flags = self.conn.recv_i32()?;
        if bind_flags < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid bind flags: {bind_flags}"),
            ));
        }

        Ok(bind_flags as u32)
    }

    pub(crate) fn get_xdp_flags(&mut self) -> io::Result<u32> {
        self.conn.write_all(&FLASH_GET_XDP_FLAGS)?;

        let xdp_flags = self.conn.recv_i32()?;
        if xdp_flags < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid xdp flags: {xdp_flags}"),
            ));
        }

        Ok(xdp_flags as u32)
    }

    pub(crate) fn get_mode(&mut self) -> io::Result<u32> {
        self.conn.write_all(&FLASH_GET_MODE)?;

        let mode = self.conn.recv_i32()?;
        if mode < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid xsk mode: {mode}"),
            ));
        }

        Ok(mode as u32)
    }

    pub(crate) fn get_poll_timeout(&mut self) -> io::Result<i32> {
        self.conn.write_all(&FLASH_GET_POLL_TIMEOUT)?;
        self.conn.recv_i32()
    }

    pub(crate) fn get_frags_enabled(&mut self) -> io::Result<bool> {
        self.conn.write_all(&FLASH_GET_FRAGS_ENABLED)?;
        self.conn.recv_bool()
    }

    pub(crate) fn get_ifname(&mut self) -> io::Result<String> {
        self.conn.write_all(&FLASH_GET_IFNAME)?;
        self.conn.recv_string::<16>()
    }

    pub(crate) fn get_ip_addr(&mut self) -> io::Result<String> {
        self.conn.write_all(&FLASH_GET_IP_ADDR)?;
        self.conn.recv_string::<16>()
    }

    pub(crate) fn get_dst_ip_addr(&mut self) -> io::Result<Vec<String>> {
        self.conn.write_all(&FLASH_GET_DST_IP_ADDR)?;
        let n = self.conn.recv_i32()?;
        if n < 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("invalid dst ip addr size: {n}"),
            ));
        }

        (0..n as usize)
            .map(|_| self.conn.recv_string::<16>())
            .collect::<Result<Vec<_>, _>>()
    }

    pub(crate) fn set_nonblocking(&mut self) -> io::Result<()> {
        self.conn.set_nonblocking(true)
    }
}

impl Drop for UdsClient {
    fn drop(&mut self) {
        #[cfg(feature = "tracing")]
        if let Err(err) = self.conn.write_all(&FLASH_CLOSE_CONN) {
            tracing::error!("Error sending FLASH_CLOSE_CONN: {err}");
        } else {
            tracing::debug!("Sent FLASH_CLOSE_CONN: {FLASH_CLOSE_CONN:?}");
        }

        if let Err(err) = self.conn.write_all(&FLASH_CLOSE_CONN) {
            eprintln!("error closing flash connection: {err}");
        }
    }
}
