const ETHER_TYPE_IPV4: u16 = 0x0800;

const IP_PROTO_ICMP: u8 = 1;

const ICMP_ECHO_REPLY: u8 = 0;
const ICMP_ECHO_REQUEST: u8 = 8;

#[forbid(clippy::indexing_slicing)]
#[allow(clippy::cast_possible_truncation)]
#[inline]
pub fn echo_reply(pkt: &mut [u8; 54]) -> bool {
    if u16::from_be_bytes([pkt[12], pkt[13]]) != ETHER_TYPE_IPV4
        || pkt[23] != IP_PROTO_ICMP
        || pkt[34] != ICMP_ECHO_REQUEST
    {
        return false;
    }

    let mut tmp = [0u8; 6];

    tmp.copy_from_slice(&pkt[0..6]);
    pkt[6..12].swap_with_slice(&mut tmp);
    pkt[0..6].copy_from_slice(&tmp);

    tmp[0..4].copy_from_slice(&pkt[26..30]);
    pkt[30..34].swap_with_slice(&mut tmp[0..4]);
    pkt[26..30].copy_from_slice(&tmp[0..4]);

    pkt[34] = ICMP_ECHO_REPLY;

    let old_csum = u16::from_be_bytes([pkt[36], pkt[37]]);
    let mut csum = u32::from(!old_csum) + !0x0800;
    if csum > 0xFFFF {
        csum = (csum & 0xFFFF) + (csum >> 16);
    }
    let csum = !(csum as u16);

    pkt[36..38].copy_from_slice(&csum.to_be_bytes());

    true
}
