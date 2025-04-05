#![allow(dead_code)]

use std::{
    io::{self, Read, Write as _},
    os::unix::net::UnixStream,
};

use uds::UnixStreamExt;

const UNIX_SOCKET_PATH: &str = "/tmp/flash/uds.sock";

macro_rules! flash_command {
    ($name:ident, $value:expr) => {
        pub(crate) const $name: [u8; 4] = ($value as u32).to_ne_bytes();
    };
}

flash_command!(FLASH_CREATE_UMEM, 1);
flash_command!(FLASH_GET_UMEM, 2);
flash_command!(FLASH_CREATE_SOCKET, 3);
flash_command!(FLASH_CLOSE_CONN, 4);
flash_command!(FLASH_GET_THREAD_INFO, 5);
flash_command!(FLASH_GET_UMEM_OFFSET, 6);
flash_command!(FLASH_GET_ROUTE_INFO, 7);
flash_command!(FLASH_GET_BIND_FLAGS, 8);
flash_command!(FLASH_GET_XDP_FLAGS, 9);
flash_command!(FLASH_GET_MODE, 10);
flash_command!(FLASH_GET_POLL_TIMEOUT, 11);
flash_command!(FLASH_GET_FRAGS_ENABLED, 12);
flash_command!(FLASH_GET_IFNAME, 13);
flash_command!(FLASH_GET_IP_ADDR, 14);
flash_command!(FLASH_GET_DST_IP_ADDR, 15);

#[derive(Debug)]
pub(crate) struct UdsConn(UnixStream);

impl UdsConn {
    pub(crate) fn new() -> io::Result<Self> {
        Ok(Self(UnixStream::connect(UNIX_SOCKET_PATH)?))
    }

    #[inline]
    pub(crate) fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        self.0.write_all(buf)
    }

    #[inline]
    pub(crate) fn recv_fd(&self) -> io::Result<i32> {
        let mut buf = [0; 1];
        self.0.recv_fds(&mut [0; 1], &mut buf)?;

        Ok(buf[0])
    }

    #[inline]
    pub(crate) fn recv_i32(&mut self) -> io::Result<i32> {
        let mut buf = [0; 4];
        self.0.read_exact(&mut buf)?;

        Ok(i32::from_ne_bytes(buf))
    }

    #[inline]
    pub(crate) fn recv_bool(&mut self) -> io::Result<bool> {
        let mut buf = [0; 1];
        self.0.read_exact(&mut buf)?;

        Ok(buf[0] != 0)
    }

    #[inline]
    pub(crate) fn recv_string(&mut self) -> io::Result<String> {
        let mut buf = [0; 16];
        self.0.read_exact(&mut buf)?;

        Ok(String::from_utf8_lossy(&buf).trim().to_string())
    }

    #[inline]
    pub(crate) fn set_nonblocking(&self, nonblocking: bool) -> io::Result<()> {
        self.0.set_nonblocking(nonblocking)
    }
}

impl Drop for UdsConn {
    fn drop(&mut self) {
        #[cfg(feature = "tracing")]
        if let Err(err) = self.0.write_all(&FLASH_CLOSE_CONN) {
            tracing::error!("Error sending FLASH_CLOSE_CONN: {err}");
        } else {
            tracing::debug!("Sent FLASH_CLOSE_CONN: {FLASH_CLOSE_CONN:?}");
        }

        let _ = self.0.write_all(&FLASH_CLOSE_CONN);
    }
}
