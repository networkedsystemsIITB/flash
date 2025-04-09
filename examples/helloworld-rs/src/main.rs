use clap::Parser;
use flash::FlashConfig;

fn main() {
    #[cfg(feature = "tracing")]
    tracing_subscriber::fmt::init();

    let flash_config = FlashConfig::parse();

    let (sockets, _) = flash::connect(&flash_config, false).unwrap();
    if sockets.is_empty() {
        eprintln!("No sockets received");
    }
}
