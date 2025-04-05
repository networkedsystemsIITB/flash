use clap::Parser;

#[derive(Debug, Parser)]
pub struct Cli {
    #[arg(short = 'f', long)]
    pub nf_id: u32,

    #[arg(short, long)]
    pub umem_id: u32,

    #[arg(short = 'b', long = "apply-bp")]
    pub back_pressure: bool,

    #[arg(short = 's', long)]
    pub fwd_all: bool,
}
