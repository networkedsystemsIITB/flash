use clap::Parser;
use flash::FlashConfig;

fn main() {
    #[cfg(feature = "tracing")]
    tracing_subscriber::fmt::init();

    let flash_config = FlashConfig::parse();

    let sockets = match flash::connect(&flash_config) {
        Ok((sockets, _)) => sockets,
        Err(err) => {
            eprintln!("{err}");
            return;
        }
    };

    if sockets.is_empty() {
        eprintln!("no sockets received");
        #[cfg(feature = "tracing")]
        return;
    }

    #[cfg(feature = "tracing")]
    tracing::info!("sockets: {sockets:?}");
}
