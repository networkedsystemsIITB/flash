use chrono::Local;
use ratatui::{
    Frame,
    layout::{Constraint, Rect},
    text::ToText as _,
    widgets::{Row, Table},
};

use crate::stats::Stats;

use super::max_len;

const HEADERS: [&str; 3] = ["interface", "xdp flags", "timestamp"];
const MAX_HEADER_LEN: u16 = max_len!(HEADERS);

impl Stats {
    pub(crate) fn render(&self, frame: &mut Frame<'_>, area: Rect) {
        let interface = format!("{}:{}", self.interface.name, self.interface.queue);
        let xdp_flags = format!("{:?}", self.xdp_flags);
        let tstamp = Local::now().format("%H:%M:%S%.3f");

        let rows = [
            Row::new([HEADERS[0].to_text(), interface.to_text()]),
            Row::new([HEADERS[1].to_text(), xdp_flags.to_text()]),
            Row::new([HEADERS[2].to_text(), tstamp.to_text()]),
        ];

        let table = Table::new(
            rows,
            [Constraint::Length(MAX_HEADER_LEN), Constraint::Fill(1)],
        )
        .header(Row::new([""; 0]));

        frame.render_widget(table, area);
    }
}
