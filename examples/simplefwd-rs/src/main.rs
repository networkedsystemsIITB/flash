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

fn socket_thread(mut socket: Socket, run: &Arc<AtomicBool>) {
    while run.load(Ordering::SeqCst) {
        let descs = socket.recv().unwrap();
        socket.send(descs);
    }
}

fn main() {
    #[cfg(feature = "tracing")]
    tracing_subscriber::fmt::init();

    let cli = Cli::parse();

    let sockets = flash::connect(cli.nf_id, cli.umem_id, cli.back_pressure, cli.fwd_all).unwrap();
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
