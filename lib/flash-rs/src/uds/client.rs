use crate::util;

use super::{
    conn::UdsConn,
    def::{
        FLASH_CLOSE_CONN, FLASH_CREATE_SOCKET, FLASH_GET_BIND_FLAGS, FLASH_GET_DST_IP_ADDR,
        FLASH_GET_FRAGS_ENABLED, FLASH_GET_IFNAME, FLASH_GET_IP_ADDR, FLASH_GET_MODE,
        FLASH_GET_POLLOUT_STATUS, FLASH_GET_PREV_NF, FLASH_GET_ROUTE_INFO, FLASH_GET_UMEM,
        FLASH_GET_UMEM_OFFSET, FLASH_GET_XDP_FLAGS,
    },
    error::{UdsError, UdsResult},
};

const FLASH_UNIX_SOCKET_PATH: &str = "/tmp/flash/uds.sock";

#[derive(Debug)]
pub(crate) struct UdsClient {
    conn: UdsConn,
}

#[allow(dead_code, clippy::cast_sign_loss)]
impl UdsClient {
    pub(crate) fn new() -> UdsResult<Self> {
        Ok(Self {
            conn: UdsConn::new(FLASH_UNIX_SOCKET_PATH)?,
        })
    }

    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::similar_names
    )]
    pub(crate) fn get_umem(
        &mut self,
        umem_id: usize,
        nf_id: usize,
    ) -> UdsResult<(i32, usize, usize, u32)> {
        #[repr(C)]
        struct NfData {
            umem_id: i32,
            nf_id: i32,
        }

        self.conn.write_all(&FLASH_GET_UMEM)?;
        self.conn.write_all(util::as_bytes(&NfData {
            umem_id: umem_id as i32,
            nf_id: nf_id as i32,
        }))?;

        let umem_fd = self.conn.recv_fd()?;
        if umem_fd < 0 {
            return Err(UdsError::InvalidUmemFd);
        }

        let total_sockets = self.conn.recv_i32()?;
        if total_sockets <= 0 {
            return Err(UdsError::InvalidTotalSockets);
        }

        let umem_size = self.conn.recv_i32()?;
        if umem_size <= 0 {
            return Err(UdsError::InvalidUmemSize);
        }

        let umem_scale = self.conn.recv_i32()?;
        if umem_scale <= 0 {
            return Err(UdsError::InvalidUmemScale);
        }

        Ok((
            umem_fd,
            total_sockets as usize,
            umem_size as usize,
            umem_scale as u32,
        ))
    }

    pub(crate) fn create_socket(&mut self) -> UdsResult<(i32, u32)> {
        self.conn.write_all(&FLASH_CREATE_SOCKET)?;

        let fd = self.conn.recv_fd()?;
        let ifqueue = self.conn.recv_i32()?;

        if fd < 0 {
            Err(UdsError::InvalidSocketFd)
        } else if ifqueue < 0 {
            Err(UdsError::InvalidSocketIfqueue)
        } else {
            Ok((fd, ifqueue as u32))
        }
    }

    pub(crate) fn get_umem_offset(&mut self) -> UdsResult<u64> {
        self.conn.write_all(&FLASH_GET_UMEM_OFFSET)?;
        let umem_offset = self.conn.recv_i32()?;

        if umem_offset < 0 {
            Err(UdsError::InvalidUmemOffset)
        } else {
            Ok(umem_offset as u64)
        }
    }

    pub(crate) fn get_route_info(&mut self) -> UdsResult<usize> {
        self.conn.write_all(&FLASH_GET_ROUTE_INFO)?;
        let route_size = self.conn.recv_i32()?;

        if route_size < 0 {
            Err(UdsError::InvalidNextSize)
        } else {
            Ok(route_size as usize)
        }
    }

    pub(crate) fn get_bind_flags(&mut self) -> UdsResult<u32> {
        self.conn.write_all(&FLASH_GET_BIND_FLAGS)?;
        Ok(self.conn.recv_u32()?)
    }

    pub(crate) fn get_xdp_flags(&mut self) -> UdsResult<u32> {
        self.conn.write_all(&FLASH_GET_XDP_FLAGS)?;
        Ok(self.conn.recv_u32()?)
    }

    pub(crate) fn get_mode(&mut self) -> UdsResult<u32> {
        self.conn.write_all(&FLASH_GET_MODE)?;
        Ok(self.conn.recv_u32()?)
    }

    // pub(crate) fn get_poll_timeout(&mut self) -> UdsResult<i32> {
    //     self.conn.write_all(&FLASH_GET_POLL_TIMEOUT)?;
    //     Ok(self.conn.recv_i32()?)
    // }

    pub(crate) fn get_frags_enabled(&mut self) -> UdsResult<bool> {
        self.conn.write_all(&FLASH_GET_FRAGS_ENABLED)?;
        Ok(self.conn.recv_bool()?)
    }

    pub(crate) fn get_ifname(&mut self) -> UdsResult<String> {
        self.conn.write_all(&FLASH_GET_IFNAME)?;
        Ok(self.conn.recv_string::<16>()?)
    }

    pub(crate) fn get_ip_addr(&mut self) -> UdsResult<String> {
        self.conn.write_all(&FLASH_GET_IP_ADDR)?;
        Ok(self.conn.recv_string::<16>()?)
    }

    pub(crate) fn get_dst_ip_addr(&mut self) -> UdsResult<Vec<String>> {
        self.conn.write_all(&FLASH_GET_DST_IP_ADDR)?;

        let dst_size = self.conn.recv_i32()?;
        if dst_size < 0 {
            return Err(UdsError::InvalidNextSize);
        }

        let mut dst_ip_addr = Vec::with_capacity(dst_size as usize);
        for _ in 0..dst_size {
            dst_ip_addr.push(self.conn.recv_string::<16>()?);
        }

        Ok(dst_ip_addr)
    }

    pub(crate) fn get_pollout_status(&mut self) -> UdsResult<(i32, usize)> {
        self.conn.write_all(&FLASH_GET_POLLOUT_STATUS)?;

        let pollout_fd = self.conn.recv_fd()?;
        if pollout_fd < 0 {
            return Err(UdsError::InvalidPollOutFd);
        }

        let pollout_size = self.conn.recv_i32()?;
        if pollout_size < 0 {
            return Err(UdsError::InvalidPollOutSize);
        }

        Ok((pollout_fd, pollout_size as usize))
    }

    #[allow(clippy::cast_possible_truncation)]
    pub(crate) fn get_prev_nf(&mut self) -> UdsResult<Vec<usize>> {
        self.conn.write_all(&FLASH_GET_PREV_NF)?;

        let prev_size = self.conn.recv_i32()?;
        if prev_size < 0 {
            return Err(UdsError::InvalidPrevSize);
        }

        let mut prev_nf_ids = Vec::with_capacity(prev_size as usize);
        for _ in 0..prev_size {
            let prev_nf = self.conn.recv_i32()?;
            if prev_nf < 0 {
                return Err(UdsError::InvalidPrevNfId);
            }

            prev_nf_ids.push(prev_nf as usize);
        }

        Ok(prev_nf_ids)
    }

    pub(crate) fn set_nonblocking(&mut self) -> UdsResult<()> {
        Ok(self.conn.set_nonblocking(true)?)
    }
}

impl Drop for UdsClient {
    fn drop(&mut self) {
        #[cfg(feature = "tracing")]
        if let Err(err) = self.conn.write_all(&FLASH_CLOSE_CONN) {
            tracing::error!("error closing flash connection: {err}");
        } else {
            tracing::debug!("Sent FLASH_CLOSE_CONN: {FLASH_CLOSE_CONN:?}");
        }

        if let Err(err) = self.conn.write_all(&FLASH_CLOSE_CONN) {
            eprintln!("error closing flash connection: {err}");
        }
    }
}
