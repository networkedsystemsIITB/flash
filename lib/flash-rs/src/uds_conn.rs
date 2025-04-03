use std::{
    io::{self, Read, Write as _},
    os::unix::net::UnixStream,
};

use tracing::{error, info};
use uds::UnixStreamExt;

use crate::def::{FLASH_CLOSE_CONN, UNIX_SOCKET_PATH};

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
        if let Err(err) = self.0.write_all(&FLASH_CLOSE_CONN) {
            error!("Failed to send FLASH_CLOSE_CONN: {err}");
        } else {
            info!("Sent FLASH_CLOSE_CONN: {FLASH_CLOSE_CONN:?}");
        }
    }
}
