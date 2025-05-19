mod cli;
mod maglev;
mod nf;

use std::{
    hash::BuildHasher,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    thread,
};

use clap::Parser;
use flash::{Route, Socket};
use fnv::FnvBuildHasher;
use macaddr::MacAddr6;

use crate::{cli::Cli, maglev::Maglev};

const MAGLEV_TABLE_SIZE: usize = 65537;

fn socket_thread<H: BuildHasher + Default>(
    mut socket: Socket,
    maglev: &Arc<Maglev<H>>,
    route: &Arc<Route>,
    next_mac: &Arc<Vec<MacAddr6>>,
    run: &Arc<AtomicBool>,
) {
    while run.load(Ordering::SeqCst) {
        if !socket.poll().is_ok_and(|val| val) {
            continue;
        }

        let Ok(descs) = socket.recv() else {
            continue;
        };

        let mut descs_send = Vec::with_capacity(descs.len());
        let mut descs_drop = Vec::new();

        for mut desc in descs {
            if let Ok(pkt) = socket.read_exact(&desc) {
                if let Some(idx) = nf::load_balance(pkt, maglev, route, next_mac) {
                    desc.set_next(idx);
                    descs_send.push(desc);
                } else {
                    descs_drop.push(desc);
                }
            } else {
                descs_drop.push(desc);
            }
        }

        socket.send(descs_send);
        socket.drop(descs_drop);
    }
}

fn main() {
    #[cfg(feature = "tracing")]
    tracing_subscriber::fmt::init();

    let cli = Cli::parse();

    let (sockets, route) = match flash::connect(&cli.flash_config) {
        Ok(t) => t,
        Err(err) => {
            eprintln!("{err}");
            return;
        }
    };

    if sockets.is_empty() {
        eprintln!("no sockets received");
        return;
    }

    if route.next.is_empty() {
        eprintln!("empty route received");
        return;
    }

    if cli.next_mac.len() > 1 && cli.next_mac.len() != route.next.len() {
        eprintln!(
            "number of next NF MACs ({}) does not match number of next NFs ({})",
            cli.next_mac.len(),
            route.next.len()
        );
        return;
    }

    let maglev = Arc::new(Maglev::<FnvBuildHasher>::new(
        &route.next,
        MAGLEV_TABLE_SIZE,
    ));
    let route = Arc::new(route);
    let next_mac = Arc::new(cli.next_mac);

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
            let maglev = maglev.clone();
            let route = route.clone();
            let next_macs = next_mac.clone();

            thread::spawn(move || {
                core_affinity::set_for_current(core_id);
                socket_thread(socket, &maglev, &route, &next_macs, &r);
            })
        })
        .collect::<Vec<_>>();

    for handle in handles {
        if let Err(err) = handle.join() {
            eprintln!("error in thread: {err:?}");
        }
    }
}
