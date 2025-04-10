use std::{
    io::{self, Read, Write as _},
    os::unix::net::UnixStream,
    path::Path,
};

use uds::UnixStreamExt;

#[derive(Debug)]
pub(super) struct UdsConn(UnixStream);

impl UdsConn {
    pub(super) fn new<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        Ok(Self(UnixStream::connect(path)?))
    }

    #[inline]
    pub(super) fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        self.0.write_all(buf)
    }

    #[inline]
    pub(super) fn recv_fd(&self) -> io::Result<i32> {
        let mut buf = [0; 1];
        self.0.recv_fds(&mut [0; 1], &mut buf)?;

        Ok(buf[0])
    }

    #[inline]
    pub(super) fn recv_i32(&mut self) -> io::Result<i32> {
        let mut buf = [0; 4];
        self.0.read_exact(&mut buf)?;

        Ok(i32::from_ne_bytes(buf))
    }

    #[inline]
    pub(super) fn recv_bool(&mut self) -> io::Result<bool> {
        let mut buf = [0; 1];
        self.0.read_exact(&mut buf)?;

        Ok(buf[0] != 0)
    }

    #[inline]
    pub(super) fn recv_string<const SIZE: usize>(&mut self) -> io::Result<String> {
        let mut buf = [0; SIZE];
        self.0.read_exact(&mut buf)?;

        Ok(String::from_utf8_lossy(&buf)
            .trim_end_matches('\0')
            .to_string())
    }

    #[inline]
    pub(super) fn set_nonblocking(&self, nonblocking: bool) -> io::Result<()> {
        self.0.set_nonblocking(nonblocking)
    }
}
