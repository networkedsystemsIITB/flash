mod cli;
mod nf;

use std::{
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    thread,
};

use clap::Parser;
use flash::Socket;
use macaddr::MacAddr6;

use crate::{cli::Cli, nf::Firewall};

fn socket_thread(
    mut socket: Socket,
    firewall: &Arc<Firewall>,
    mac_addr: Option<MacAddr6>,
    run: &Arc<AtomicBool>,
) {
    while run.load(Ordering::SeqCst) {
        if !socket.poll().is_ok_and(|val| val) {
            continue;
        }

        let Ok(descs) = socket.recv() else {
            continue;
        };

        let (descs_send, descs_drop) = descs.into_iter().partition(|desc| {
            socket
                .read(desc)
                .is_ok_and(|pkt| nf::firewall_filter(pkt, firewall, mac_addr))
        });

        socket.send(descs_send);
        socket.drop(descs_drop);
    }
}

fn main() {
    #[cfg(feature = "tracing")]
    tracing_subscriber::fmt::init();

    let cli = Cli::parse();

    let sockets = match flash::connect(&cli.flash_config) {
        Ok((sockets, _)) => sockets,
        Err(err) => {
            eprintln!("{err}");
            return;
        }
    };

    if sockets.is_empty() {
        eprintln!("no sockets received");
        return;
    }

    let firewall = match Firewall::new(cli.denylist) {
        Ok(firewall) => Arc::new(firewall),
        Err(err) => {
            eprintln!("error loading firewall rules: {err}");
            return;
        }
    };

    let cores = core_affinity::get_core_ids()
        .unwrap_or_default()
        .into_iter()
        .filter(|core_id| core_id.id >= cli.cpu_start && core_id.id <= cli.cpu_end)
        .collect::<Vec<_>>();

    if cores.is_empty() {
        eprintln!("no cores found in range {}-{}", cli.cpu_start, cli.cpu_end);
        return;
    }

    #[cfg(feature = "tracing")]
    tracing::debug!("Cores: {:?}", cores);

    let run = Arc::new(AtomicBool::new(true));

    let r = run.clone();
    if let Err(err) = ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    }) {
        eprintln!("error setting Ctrl-C handler: {err}");
        return;
    }

    let handles = sockets
        .into_iter()
        .zip(cores.into_iter().cycle())
        .map(|(socket, core_id)| {
            let r = run.clone();
            let firewall = firewall.clone();

            thread::spawn(move || {
                core_affinity::set_for_current(core_id);
                socket_thread(socket, &firewall, cli.mac_addr, &r);
            })
        })
        .collect::<Vec<_>>();

    for handle in handles {
        if let Some(err) = handle.join().err() {
            eprintln!("error in thread: {err:?}");
        }
    }
}
