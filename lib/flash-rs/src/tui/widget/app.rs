use ratatui::{
    Frame,
    layout::{Constraint, Rect},
    prelude::Alignment,
    style::{Color, Style},
    text::ToText as _,
    widgets::{Row, Table},
};

use crate::stats::AppStats;

use super::max_len;

const HEADERS: [&str; 6] = [
    "rx empty polls",
    "fill fail polls",
    "tx copy sendtos",
    "tx wakeup sendtos",
    "opt polls",
    "backpressure",
];
const MAX_HEADER_LEN: u16 = max_len!(HEADERS);

impl AppStats {
    pub(crate) fn render(&self, old_stats: &Self, diff: u64, frame: &mut Frame<'_>, area: Rect) {
        let rx_empty_polls_cps =
            ((self.rx_empty_polls - old_stats.rx_empty_polls) * 1_000_000_000) / diff;
        let fill_fail_polls_cps =
            ((self.fill_fail_polls - old_stats.fill_fail_polls) * 1_000_000_000) / diff;
        let tx_copy_sendtos_cps =
            ((self.tx_copy_sendtos - old_stats.tx_copy_sendtos) * 1_000_000_000) / diff;
        let tx_wakeup_sendtos_cps =
            ((self.tx_wakeup_sendtos - old_stats.tx_wakeup_sendtos) * 1_000_000_000) / diff;
        let opt_polls_cps = ((self.opt_polls - old_stats.opt_polls) * 1_000_000_000) / diff;
        let backpressure_cps =
            ((self.backpressure - old_stats.backpressure) * 1_000_000_000) / diff;

        let rows = [
            Row::new([
                HEADERS[0].to_text(),
                rx_empty_polls_cps.to_text().alignment(Alignment::Right),
                self.rx_empty_polls.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[1].to_text(),
                fill_fail_polls_cps.to_text().alignment(Alignment::Right),
                self.fill_fail_polls.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[2].to_text(),
                tx_copy_sendtos_cps.to_text().alignment(Alignment::Right),
                self.tx_copy_sendtos.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[3].to_text(),
                tx_wakeup_sendtos_cps.to_text().alignment(Alignment::Right),
                self.tx_wakeup_sendtos.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[4].to_text(),
                opt_polls_cps.to_text().alignment(Alignment::Right),
                self.opt_polls.to_text().alignment(Alignment::Right),
            ]),
            Row::new([
                HEADERS[5].to_text(),
                backpressure_cps.to_text().alignment(Alignment::Right),
                self.backpressure.to_text().alignment(Alignment::Right),
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
                "App".to_text(),
                "calls/s".to_text().alignment(Alignment::Right),
                "count".to_text().alignment(Alignment::Right),
            ])
            .style(Style::default().fg(Color::Yellow)),
        );

        frame.render_widget(table, area);
    }
}
