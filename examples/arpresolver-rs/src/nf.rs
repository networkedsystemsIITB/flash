use std::net::Ipv4Addr;

use macaddr::MacAddr6;

const ETHER_TYPE_ARP: u16 = 0x0806;

const ARP_HTYPE_ETHERNET: u16 = 0x0001;
const ARP_PTYPE_IPV4: u16 = 0x0800;
const ARP_HLEN_ETHERNET: u8 = 6;
const ARP_PLEN_IPV4: u8 = 4;
const ARP_OPCODE_REQUEST: u16 = 1;
const ARP_OPCODE_REPLY: u16 = 2;

#[forbid(clippy::indexing_slicing)]
#[inline]
pub fn arp_resolve(pkt: &mut [u8; 42], nf_addr: MacAddr6, nf_ip: Ipv4Addr) -> bool {
    if u16::from_be_bytes([pkt[12], pkt[13]]) != ETHER_TYPE_ARP
        || u16::from_be_bytes([pkt[14], pkt[15]]) != ARP_HTYPE_ETHERNET
        || u16::from_be_bytes([pkt[16], pkt[17]]) != ARP_PTYPE_IPV4
        || pkt[18] != ARP_HLEN_ETHERNET
        || pkt[19] != ARP_PLEN_IPV4
        || u16::from_be_bytes([pkt[20], pkt[21]]) != ARP_OPCODE_REQUEST
    {
        return false;
    }

    if pkt[38..42] != nf_ip.octets() {
        return false;
    }

    let mut tmp = [0u8; 6];
    tmp.copy_from_slice(&pkt[6..12]);

    pkt[0..6].copy_from_slice(&tmp);
    pkt[32..38].copy_from_slice(&tmp);

    pkt[6..12].copy_from_slice(&nf_addr.into_array());
    pkt[22..28].copy_from_slice(&nf_addr.into_array());

    pkt[20..22].copy_from_slice(&ARP_OPCODE_REPLY.to_be_bytes());

    tmp[0..4].copy_from_slice(&pkt[28..32]);
    pkt[38..42].swap_with_slice(&mut tmp[0..4]);
    pkt[28..32].copy_from_slice(&tmp[0..4]);

    true
}
