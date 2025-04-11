#![forbid(clippy::indexing_slicing)]

use std::{net::Ipv4Addr, path::Path};

use csv::Reader;
use serde::{Deserialize, de};

const ETHER_TYPE_IPV4: u16 = 0x0800;

const IP_PROTO_TCP: u8 = 6;
const IP_PROTO_UDP: u8 = 17;

pub struct Firewall {
    rules: Vec<Tuple5>,
}

impl Firewall {
    pub fn new(path: impl AsRef<Path>) -> csv::Result<Self> {
        Ok(Self {
            rules: Reader::from_path(path)?
                .deserialize()
                .collect::<Result<Vec<_>, _>>()?,
        })
    }

    #[inline]
    pub fn filter(&self, pkt: &mut [u8; 54]) -> bool {
        if u16::from_be_bytes([pkt[12], pkt[13]]) != ETHER_TYPE_IPV4 {
            return false;
        }

        let Some(tuple) = Tuple5::new(pkt) else {
            return false;
        };

        !self.rules.iter().any(|rule| *rule == tuple)
    }
}

#[derive(Deserialize, Eq, PartialEq)]
struct Tuple5 {
    #[serde(deserialize_with = "deserialize_proto")]
    ip_proto: u8,

    src_addr: Ipv4Addr,
    dst_addr: Ipv4Addr,

    src_port: u16,
    dst_port: u16,
}

fn deserialize_proto<'de, D>(deserializer: D) -> Result<u8, D::Error>
where
    D: serde::de::Deserializer<'de>,
{
    let value = String::deserialize(deserializer)?;
    match value.to_uppercase().as_str() {
        "TCP" => Ok(IP_PROTO_TCP),
        "UDP" => Ok(IP_PROTO_UDP),
        _ => match value.parse::<u8>() {
            Ok(IP_PROTO_TCP) => Ok(IP_PROTO_TCP),
            Ok(IP_PROTO_UDP) => Ok(IP_PROTO_UDP),
            _ => Err(de::Error::custom(format!("invalid protocol: {value}"))),
        },
    }
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
