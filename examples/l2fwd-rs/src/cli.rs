use clap::Parser;

#[allow(clippy::struct_excessive_bools)]
#[derive(Debug, Parser)]
pub struct Cli {
    #[arg(short = 'c', long, default_value_t = 0)]
    pub cpu_start: usize,

    #[arg(short = 'e', long, default_value_t = 0)]
    pub cpu_end: usize,

    #[arg(short = 't', long, default_value_t = 1)]
    pub stats_cpu: usize,

    #[arg(short = 'f', long)]
    pub nf_id: u32,

    #[arg(short, long)]
    pub umem_id: u32,

    #[arg(short = 'b', long = "apply-bp")]
    pub back_pressure: bool,

    #[arg(short = 's', long)]
    pub fwd_all: bool,
}
