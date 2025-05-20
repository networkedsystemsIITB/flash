use std::{num::ParseIntError, time::Duration};

use clap::Parser;

#[derive(Debug, Parser)]
pub struct FlashConfig {
    #[arg(short, long, help = "Umem id used to connect to monitor")]
    pub(crate) umem_id: u32,

    #[arg(short = 'f', long, help = "NF id used to connect to monitor")]
    pub(crate) nf_id: u32,

    #[arg(
        short = 'p',
        long,
        default_value_t = false,
        help = "Enable smart polling mode"
    )]
    pub(crate) smart_poll: bool,

    #[arg(
        short,
        long,
        default_value = "100",
        value_parser = parse_millis,
        help="Idle timeout for smart polling (in ms)"
    )]
    pub(crate) idle_timeout: Duration,

    #[arg(
        short = 'I',
        long,
        default_value_t = 0.,
        help = "Idleness for smart polling [0.0 = busy-polling, 1.0 = polling]"
    )]
    pub(crate) idleness: f32,

    #[arg(short,
        long,
        default_value = "0",
        value_parser = parse_micros,
        help="Sleep duration under backpressure (in \u{00B5}s)"
    )]
    pub(crate) bp_timeout: Duration,

    #[arg(
        short = 'B',
        long,
        default_value_t = 0.5,
        help = "Backpressure sensitivity [0.0 = low (0 pkts), 1.0 = high (2048 pkts)]"
    )]
    pub(crate) bp_sense: f32,
}

fn parse_millis(arg: &str) -> Result<Duration, ParseIntError> {
    Ok(Duration::from_millis(arg.parse()?))
}

fn parse_micros(arg: &str) -> Result<Duration, ParseIntError> {
    Ok(Duration::from_micros(arg.parse()?))
}
