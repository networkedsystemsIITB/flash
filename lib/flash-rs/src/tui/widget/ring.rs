use ratatui::{
    Frame,
    layout::{Constraint, Rect},
    prelude::Alignment,
    style::{Color, Style},
    text::ToText as _,
    widgets::{Row, Table},
};

use crate::stats::RingStats;

use super::max_len;

const HEADERS: [&str; 3] = ["rx", "tx", "drop"];
const MAX_HEADER_LEN: u16 = max_len!(HEADERS);

impl RingStats {
    pub(crate) fn render(&self, old_stats: &Self, diff: u64, frame: &mut Frame<'_>, area: Rect) {
        let rx_pps = ((self.rx - old_stats.rx) * 1_000_000_000) / diff;
        let tx_pps = ((self.tx - old_stats.tx) * 1_000_000_000) / diff;
        let drop_pps = ((self.drop - old_stats.drop) * 1_000_000_000) / diff;

        let rows = [
            Row::new([
                HEADERS[0].to_text(),
                rx_pps.to_text().alignment(Alignment::Right),
                self.rx.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[1].to_text(),
                tx_pps.to_text().alignment(Alignment::Right),
                self.tx.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[2].to_text(),
                drop_pps.to_text().alignment(Alignment::Right),
                self.drop.to_text().alignment(Alignment::Right),
            ]),
        ];

        let table = Table::new(
            rows,
            [
                Constraint::Length(MAX_HEADER_LEN),
                Constraint::Fill(1),
                Constraint::Fill(1),
            ],
        )
        .header(
            Row::new([
                "Ring".to_text(),
                "pps".to_text().alignment(Alignment::Right),
                "pkts".to_text().alignment(Alignment::Right),
            ])
            .style(Style::default().fg(Color::Yellow)),
        );

        frame.render_widget(table, area);
    }
}
