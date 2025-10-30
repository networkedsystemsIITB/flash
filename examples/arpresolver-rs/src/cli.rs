#[cfg(feature = "stats")]
use std::str::FromStr as _;

use clap::Parser;
use flash::FlashConfig;
use macaddr::MacAddr6;

#[cfg(feature = "stats")]
use flash::tui::GridLayout;

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

    #[arg(short = 'M', long, help = "NF MAC address")]
    pub nf_mac: MacAddr6,

    #[arg(short = 'm', long, help = "Dest MAC address")]
    pub mac_addr: Option<MacAddr6>,

    #[cfg(feature = "stats")]
    #[command(flatten)]
    pub stats: StatsConfig,
}

#[cfg(feature = "stats")]
#[derive(Debug, Parser)]
pub struct StatsConfig {
    #[arg(
        short = 's',
        long,
        default_value_t = 1,
        help = "CPU core index for stats thread"
    )]
    pub cpu: usize,

    #[arg(short = 'f', long, default_value_t = 1, help = "Tui frames per second")]
    pub fps: u64,

    #[arg(short = 'l', long, default_value_t = GridLayout::default(), value_parser = GridLayout::from_str, help = "Tui layout")]
    pub layout: GridLayout,
}
