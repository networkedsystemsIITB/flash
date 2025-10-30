use ratatui::{
    Frame,
    layout::{Constraint, Rect},
    prelude::Alignment,
    style::{Color, Style},
    text::ToText as _,
    widgets::{Row, Table},
};

use crate::stats::XdpStats;

use super::max_len;

const HEADERS: [&str; 6] = [
    "rx dropped",
    "rx invalid",
    "tx invalid",
    "rx ring full",
    "rx fill ring empty",
    "tx ring empty",
];
const MAX_HEADER_LEN: u16 = max_len!(HEADERS);

impl XdpStats {
    pub(crate) fn render(&self, old_stats: &Self, diff: u64, frame: &mut Frame<'_>, area: Rect) {
        let rx_dropped_pps = ((self.rx_dropped - old_stats.rx_dropped) * 1_000_000_000) / diff;
        let rx_invalid_pps =
            ((self.rx_invalid_descs - old_stats.rx_invalid_descs) * 1_000_000_000) / diff;
        let tx_invalid_pps =
            ((self.tx_invalid_descs - old_stats.tx_invalid_descs) * 1_000_000_000) / diff;
        let rx_ring_full_pps =
            ((self.rx_ring_full - old_stats.rx_ring_full) * 1_000_000_000) / diff;
        let rx_fill_ring_empty_pps =
            ((self.rx_fill_ring_empty_descs - old_stats.rx_fill_ring_empty_descs) * 1_000_000_000)
                / diff;
        let tx_ring_empty_pps =
            ((self.tx_ring_empty_descs - old_stats.tx_ring_empty_descs) * 1_000_000_000) / diff;

        let rows = [
            Row::new([
                HEADERS[0].to_text(),
                rx_dropped_pps.to_text().alignment(Alignment::Right),
                self.rx_dropped.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[1].to_text(),
                rx_invalid_pps.to_text().alignment(Alignment::Right),
                self.rx_invalid_descs.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[2].to_text(),
                tx_invalid_pps.to_text().alignment(Alignment::Right),
                self.tx_invalid_descs.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[3].to_text(),
                rx_ring_full_pps.to_text().alignment(Alignment::Right),
                self.rx_ring_full.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[4].to_text(),
                rx_fill_ring_empty_pps.to_text().alignment(Alignment::Right),
                self.rx_fill_ring_empty_descs
                    .to_text()
                    .alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[5].to_text(),
                tx_ring_empty_pps.to_text().alignment(Alignment::Right),
                self.tx_ring_empty_descs
                    .to_text()
                    .alignment(Alignment::Right),
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
                "XDP".to_text(),
                "pps".to_text().alignment(Alignment::Right),
                "pkts".to_text().alignment(Alignment::Right),
            ])
            .style(Style::default().fg(Color::Yellow)),
        );

        frame.render_widget(table, area);
    }
}
