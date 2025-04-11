use clap::Parser;
use flash::FlashConfig;
use macaddr::MacAddr6;

#[derive(Debug, Parser)]
pub struct Cli {
    #[command(flatten)]
    pub flash_config: FlashConfig,

    #[arg(
        short = 'c',
        long,
        default_value_t = 0,
        help = "Starting CPU core index for socket threads"
    )]
    pub cpu_start: usize,

    #[arg(
        short = 'e',
        long,
        default_value_t = 0,
        help = "Ending CPU core index for socket threads (inclusive)"
    )]
    pub cpu_end: usize,

    #[arg(short = 'm', long, help = "List of MAC for next NFs ")]
    pub mac_addr: Option<MacAddr6>,
}
