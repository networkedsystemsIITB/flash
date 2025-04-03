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

fn socket_thread(
    core_id: usize,
    mut socket: Socket,
    back_pressure: bool,
    fwd_all: bool,
    run: &Arc<AtomicBool>,
) {
    if let Some(core) = core_affinity::get_core_ids()
        .unwrap()
        .into_iter()
        .find(|&cid| cid.id == core_id)
    {
        core_affinity::set_for_current(core);
    }

    while run.load(Ordering::SeqCst) {
        let descs = socket.recv(back_pressure, fwd_all).unwrap();
        socket.send(back_pressure, descs);
    }
}

fn main() {
    tracing_subscriber::fmt::init();

    let cli = Cli::parse();
    let sockets = flash::connect(cli.nf_id, cli.umem_id).unwrap();

    let cpu_count = cli.cpu_end - cli.cpu_start + 1;
    let run = Arc::new(AtomicBool::new(true));

    let r = run.clone();
    ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    })
    .unwrap();

    let handles = sockets
        .into_iter()
        .enumerate()
        .map(|(i, socket)| {
            let core_id = i % cpu_count + cli.cpu_start;

            let r = run.clone();
            thread::spawn(move || {
                socket_thread(core_id, socket, cli.back_pressure, cli.fwd_all, &r);
            })
        })
        .collect::<Vec<_>>();

    for handle in handles {
        handle.join().unwrap();
    }
}
