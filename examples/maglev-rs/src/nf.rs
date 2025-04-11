#![forbid(clippy::indexing_slicing)]

use std::{hash::BuildHasher, net::Ipv4Addr};

use flash::Route;
use macaddr::MacAddr6;

use crate::maglev::Maglev;

const ETHER_TYPE_IPV4: u16 = 0x0800;

const IP_PROTO_TCP: u8 = 6;
const IP_PROTO_UDP: u8 = 17;

#[derive(Hash, Eq, PartialEq)]
pub struct Tuple5 {
    ip_proto: u8,
    src_addr: Ipv4Addr,
    dst_addr: Ipv4Addr,
    src_port: u16,
    dst_port: u16,
}

impl Tuple5 {
    #[inline]
    fn new(pkt: &[u8; 54]) -> Option<Self> {
        match pkt[23] {
            IP_PROTO_TCP | IP_PROTO_UDP => Some(Self {
                ip_proto: pkt[23],
                src_addr: Ipv4Addr::new(pkt[26], pkt[27], pkt[28], pkt[29]),
                dst_addr: Ipv4Addr::new(pkt[30], pkt[31], pkt[32], pkt[33]),
                src_port: u16::from_be_bytes([pkt[34], pkt[35]]),
                dst_port: u16::from_be_bytes([pkt[36], pkt[37]]),
            }),
            _ => None,
        }
    }
}

#[allow(clippy::cast_possible_truncation)]
#[inline]
pub fn load_balance<H: BuildHasher + Default>(
    pkt: &mut [u8; 54],
    maglev: &Maglev<H>,
    route: &Route,
    next_macs: &[MacAddr6],
) -> Option<usize> {
    if u16::from_be_bytes([pkt[12], pkt[13]]) != ETHER_TYPE_IPV4 {
        return None;
    }

    let tuple5 = Tuple5::new(pkt)?;
    // if tuple5.dst_addr != route.ip_addr {
    //     return None;
    // }

    let idx = maglev.lookup(&tuple5);
    let next_ip = route.next.get(idx)?.octets();
    let mut csum = u32::from(!u16::from_be_bytes([pkt[24], pkt[25]]));

    csum = csum.wrapping_add(u32::from(u16::from_be_bytes([next_ip[0], next_ip[1]])));
    csum = csum.wrapping_add(u32::from(u16::from_be_bytes([next_ip[2], next_ip[3]])));

    csum = csum.wrapping_sub(u32::from(u16::from_be_bytes([pkt[30], pkt[31]])));
    csum = csum.wrapping_sub(u32::from(u16::from_be_bytes([pkt[32], pkt[33]])));

    if (csum >> 16) != 0 {
        csum = (csum & 0xFFFF) + (csum >> 16);
    }

    pkt[24..26].copy_from_slice(&(!(csum as u16)).to_be_bytes());
    pkt[30..34].copy_from_slice(&next_ip);

    if let Some(next_mac) = next_macs.get(idx) {
        pkt[6..12].copy_from_slice(next_mac.as_bytes());
    }

    Some(idx)
}
