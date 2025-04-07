use std::{num::ParseIntError, time::Duration};

use clap::Parser;

#[derive(Debug, Parser)]
pub struct Cli {
    #[arg(short = 'f', long)]
    pub nf_id: u32,

    #[arg(short, long)]
    pub umem_id: u32,

    #[arg(short = 'p', long, default_value_t = false)]
    pub smart_poll: bool,

    #[arg(short = 'i', long, default_value = "100", value_parser = parse_millis)]
    pub idle_timeout: Duration,

    #[arg(short = 'I', long, default_value_t = 0.)]
    pub idleness: f32,

    #[arg(short = 'b', long, default_value = "0", value_parser = parse_micros)]
    pub bp_timeout: Duration,

    #[arg(short = 'B', long, default_value_t = 0.5)]
    pub bp_sense: f32,
}

fn parse_millis(arg: &str) -> Result<Duration, ParseIntError> {
    Ok(Duration::from_millis(arg.parse()?))
}

fn parse_micros(arg: &str) -> Result<Duration, ParseIntError> {
    Ok(Duration::from_micros(arg.parse()?))
}
