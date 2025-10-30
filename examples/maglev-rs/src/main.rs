mod cli;
mod maglev;
mod nf;

use std::{
    hash::BuildHasher,
    net::Ipv4Addr,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    thread,
};

use clap::Parser;
use flash::Socket;
use fnv::FnvBuildHasher;
use macaddr::MacAddr6;

#[cfg(feature = "stats")]
use flash::tui::StatsDashboard;

use crate::{cli::Cli, maglev::Maglev};

const MAGLEV_TABLE_SIZE: usize = 65537;

fn socket_thread<H: BuildHasher + Default>(
    mut socket: Socket,
    maglev: &Arc<Maglev<H>>,
    next_ip: &Arc<Vec<Ipv4Addr>>,
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
        let mut descs_drop = Vec::with_capacity(descs.len());

        for mut desc in descs {
            if let Ok(pkt) = socket.read_exact(&desc)
                && let Some(idx) = nf::load_balance(pkt, maglev, next_ip)
            {
                if let Some(next_mac) = next_mac.get(idx).or_else(|| next_mac.first()) {
                    pkt[0..6].copy_from_slice(next_mac.as_bytes());
                }

                desc.set_next(idx);
                descs_send.push(desc);
            } else {
                descs_drop.push(desc);
            }
        }

        socket.send(descs_send);
        socket.drop(descs_drop);
    }
}

#[allow(clippy::too_many_lines)]
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

    #[cfg(feature = "tracing")]
    tracing::debug!("Sockets: {:?}", sockets);

    #[cfg(feature = "stats")]
    let mut tui = match StatsDashboard::new(
        sockets.iter().map(Socket::stats),
        cli.stats.fps,
        cli.stats.layout,
    ) {
        Ok(t) => t,
        Err(err) => {
            eprintln!("error creating tui: {err}");
            return;
        }
    };

    let next_ip = if route.next.is_empty() {
        if let Some(fb_ip) = cli.fallback_ip {
            vec![fb_ip]
        } else {
            eprintln!("empty route and no fallback IP configured");
            return;
        }
    } else {
        route.next
    };

    if cli.next_mac.len() > 1 && cli.next_mac.len() != next_ip.len() {
        eprintln!(
            "number of next NF MACs ({}) does not match number of next NFs ({})",
            cli.next_mac.len(),
            next_ip.len()
        );
        return;
    }

    let maglev = Arc::new(Maglev::<FnvBuildHasher>::new(&next_ip, MAGLEV_TABLE_SIZE));
    let next_ip = Arc::new(next_ip);
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

    #[cfg(feature = "stats")]
    let Some(stats_core) = core_affinity::get_core_ids()
        .unwrap_or_default()
        .into_iter()
        .find(|core_id| core_id.id == cli.stats.cpu)
    else {
        eprintln!("no core found for stats thread {}", cli.stats.cpu);
        return;
    };

    let run = Arc::new(AtomicBool::new(true));

    #[cfg(not(feature = "stats"))]
    if let Err(err) = {
        let r = run.clone();
        ctrlc::set_handler(move || {
            r.store(false, Ordering::SeqCst);
        })
    } {
        eprintln!("error setting Ctrl-C handler: {err}");
        return;
    }

    let handles = sockets
        .into_iter()
        .zip(cores.into_iter().cycle())
        .map(|(socket, core_id)| {
            let r = run.clone();
            let maglev = maglev.clone();
            let next_ip = next_ip.clone();
            let next_mac = next_mac.clone();

            thread::spawn(move || {
                core_affinity::set_for_current(core_id);
                socket_thread(socket, &maglev, &next_ip, &next_mac, &r);
            })
        })
        .collect::<Vec<_>>();

    #[cfg(feature = "stats")]
    if let Err(err) = thread::spawn(move || {
        core_affinity::set_for_current(stats_core);
        if let Err(err) = tui.run() {
            eprintln!("error dumping stats: {err}");
        }
    })
    .join()
    {
        eprintln!("error in stats thread: {err:?}");
    }

    #[cfg(feature = "stats")]
    run.store(false, Ordering::SeqCst);

    for handle in handles {
        if let Err(err) = handle.join() {
            eprintln!("error in thread: {err:?}");
        }
    }
}
