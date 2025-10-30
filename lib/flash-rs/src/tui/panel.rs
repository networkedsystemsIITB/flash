use std::{sync::Arc, time::Instant};

use ratatui::{
    Frame,
    layout::{Constraint, Layout, Rect},
    widgets::Block,
};

use crate::stats::{AppStats, RingStats, Stats, XdpStats};

#[derive(Debug)]
pub(super) struct StatsPanel {
    view: StatsView,
    area: Rect,
    block: Block<'static>,
    inner_area: Rect,
}

impl StatsPanel {
    pub(super) fn new(index: usize, stats: Arc<Stats>) -> Self {
        Self {
            view: StatsView::new(stats),
            area: Rect::default(),
            block: Block::bordered().title(format!(" Socket {} Stats ", index + 1)),
            inner_area: Rect::default(),
        }
    }

    pub(super) fn resize(&mut self, area: Rect) {
        self.area = area;
        self.inner_area = self.block.inner(area);
    }

    pub(super) fn render(&mut self, frame: &mut Frame<'_>) {
        frame.render_widget(self.block.clone(), self.area);
        self.view.render(frame, self.inner_area);
    }
}

#[derive(Debug)]
pub struct StatsView {
    stats: Arc<Stats>,
    last_timestamp: Instant,

    ring_stats: RingStats,
    app_stats: AppStats,
    xdp_stats: XdpStats,

    v_layout: Layout,
    h_layout: Layout,
}

impl StatsView {
    pub fn new(stats: Arc<Stats>) -> Self {
        Self {
            stats,
            last_timestamp: Instant::now(),

            ring_stats: RingStats::default(),
            app_stats: AppStats::default(),
            xdp_stats: XdpStats::default(),

            v_layout: Layout::vertical(Constraint::from_fills([4, 7])),
            h_layout: Layout::horizontal(Constraint::from_fills([1; 2])).spacing(2),
        }
    }

    #[allow(clippy::cast_possible_truncation)]
    pub fn render(&mut self, frame: &mut Frame<'_>, area: Rect) {
        let [v1, v2] = self.v_layout.areas(area);
        let [h1_1, h1_2] = self.h_layout.areas(v1);
        let [h2_1, h2_2] = self.h_layout.areas(v2);

        let ring_stats = self.stats.get_ring_stats();
        let app_stats = self.stats.get_app_stats();
        let xdp_stats = self.stats.get_xdp_stats().unwrap_or(self.xdp_stats);

        let now = Instant::now();
        let diff = now.duration_since(self.last_timestamp).as_nanos() as u64;

        self.stats.render(frame, h1_1);
        ring_stats.render(&self.ring_stats, diff, frame, h1_2);
        app_stats.render(&self.app_stats, diff, frame, h2_1);
        xdp_stats.render(&self.xdp_stats, diff, frame, h2_2);

        self.ring_stats = ring_stats;
        self.app_stats = app_stats;
        self.xdp_stats = xdp_stats;
        self.last_timestamp = now;
    }
}
