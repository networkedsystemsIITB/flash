mod cli;

use clap::Parser;

use crate::cli::Cli;

fn main() {
    #[cfg(feature = "tracing")]
    tracing_subscriber::fmt::init();

    let cli = Cli::parse();

    let sockets = flash::connect(cli.nf_id, cli.umem_id, cli.back_pressure, cli.fwd_all).unwrap();
    if sockets.is_empty() {
        eprintln!("No sockets received");
    }
}
