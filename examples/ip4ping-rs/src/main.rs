mod cli;

use std::{
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    thread,
};

use clap::Parser;
use flash::Socket;

use crate::cli::Cli;

#[inline]
fn echo_reply(pkt: &mut [u8]) -> bool {
    let ether_type = u16::from_be_bytes([pkt[12], pkt[13]]);
    if ether_type != 0x0800 {
        return false;
    }

    let (eth_hdr, ipv4) = pkt.split_at_mut(14);
    if ipv4[9] != 1 {
        return false;
    }

    let (ipv4_hdr, icmp) = ipv4.split_at_mut(20);
    if icmp[0] != 8 {
        return false;
    }

    let mut tmp = [0u8; 6];

    let (src_mac, dst_mac) = eth_hdr.split_at_mut(6);
    tmp.copy_from_slice(&src_mac[0..6]);
    src_mac[0..6].copy_from_slice(&dst_mac[0..6]);
    dst_mac[0..6].copy_from_slice(&tmp);

    let (src_ip, dst_ip) = ipv4_hdr.split_at_mut(8);
    tmp[0..4].copy_from_slice(&src_ip[0..4]);
    src_ip[0..4].copy_from_slice(&dst_ip[0..4]);
    dst_ip[0..4].copy_from_slice(&tmp[0..4]);

    icmp[0] = 0;

    let old_csum = u16::from_be_bytes([icmp[2], icmp[3]]);
    let mut csum = u32::from(!old_csum) + !0x0800;
    if csum > 0xFFFF {
        csum = (csum & 0xFFFF) + (csum >> 16);
    }
    csum = !csum;

    icmp[2..4].copy_from_slice(&csum.to_be_bytes());

    true
}

fn socket_thread(mut socket: Socket, run: &Arc<AtomicBool>) {
    while run.load(Ordering::SeqCst) {
        let descs = socket.recv().unwrap();

        let (descs_send, descs_drop) = descs
            .into_iter()
            .partition(|desc| echo_reply(socket.read(desc).unwrap()));

        socket.send(descs_send);
        socket.drop(descs_drop);
    }
}

fn main() {
    #[cfg(feature = "tracing")]
    tracing_subscriber::fmt::init();

    let cli = Cli::parse();

    let (sockets, _) = flash::connect(&cli.flash_config, false).unwrap();
    if sockets.is_empty() {
        eprintln!("No sockets received");
        return;
    }

    let cores = core_affinity::get_core_ids()
        .unwrap()
        .into_iter()
        .filter(|core_id| core_id.id >= cli.cpu_start && core_id.id <= cli.cpu_end)
        .collect::<Vec<_>>();

    if cores.is_empty() {
        eprintln!("No cores found in range {}-{}", cli.cpu_start, cli.cpu_end);
        return;
    }

    #[cfg(feature = "tracing")]
    tracing::debug!("Cores: {:?}", cores);

    let run = Arc::new(AtomicBool::new(true));

    let r = run.clone();
    ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    })
    .unwrap();

    let handles = sockets
        .into_iter()
        .zip(cores.into_iter().cycle())
        .map(|(socket, core_id)| {
            let r = run.clone();
            thread::spawn(move || {
                core_affinity::set_for_current(core_id);
                socket_thread(socket, &r);
            })
        })
        .collect::<Vec<_>>();

    for handle in handles {
        handle.join().unwrap();
    }
}
