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
use macaddr::MacAddr6;

#[cfg(feature = "stats")]
use flash::tui::StatsDashboard;

use crate::cli::Cli;

fn socket_thread(mut socket: Socket, mac_addr: Option<MacAddr6>, run: &Arc<AtomicBool>) {
    while run.load(Ordering::SeqCst) {
        if !socket.poll().is_ok_and(|val| val) {
            continue;
        }

        let Ok(descs) = socket.recv() else {
            continue;
        };

        let Some(mac_addr) = mac_addr else {
            socket.send(descs);
            continue;
        };

        let mut descs_send = Vec::with_capacity(descs.len());
        let mut descs_drop = Vec::with_capacity(descs.len());

        for desc in descs {
            let Ok(pkt) = socket.read_exact::<6>(&desc) else {
                descs_drop.push(desc);
                continue;
            };

            pkt[0..6].copy_from_slice(mac_addr.as_bytes());
            descs_send.push(desc);
        }

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

    #[cfg(feature = "tracing")]
    tracing::info!("Sockets: {sockets:?}");

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
            thread::spawn(move || {
                core_affinity::set_for_current(core_id);
                socket_thread(socket, cli.mac_addr, &r);
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
