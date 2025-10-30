use std::iter;

use ratatui::layout::{Constraint, Direction, Layout, Rect};

#[derive(Debug)]
pub(super) struct LayoutCache {
    config: LayoutConfig,
    cached_areas: Vec<Rect>,
    last_terminal_size: (u16, u16),
    num_panels: usize,
}

impl LayoutCache {
    pub(super) fn new(config: GridLayout, num_panels: usize) -> Self {
        Self {
            config: config.into(),
            cached_areas: Vec::new(),
            last_terminal_size: (0, 0),
            num_panels,
        }
    }

    pub(super) fn update_panel_areas(&mut self, terminal_size: (u16, u16)) -> Option<&[Rect]> {
        if self.last_terminal_size == terminal_size {
            None
        } else {
            self.recalculate_layout(terminal_size);
            Some(&self.cached_areas)
        }
    }

    fn recalculate_layout(&mut self, size: (u16, u16)) {
        let container = Rect {
            x: 0,
            y: 0,
            width: size.0,
            height: size.1,
        };

        self.cached_areas = self
            .config
            .calculate_panel_areas(self.num_panels, container);
        self.last_terminal_size = size;
    }
}

#[derive(Debug)]
struct LayoutConfig {
    primary_direction: Direction,
    panels_per_line: usize,
}

impl LayoutConfig {
    fn calculate_panel_areas(&self, num_panels: usize, container: Rect) -> Vec<Rect> {
        let line_direction = match self.primary_direction {
            Direction::Horizontal => Direction::Vertical,
            Direction::Vertical => Direction::Horizontal,
        };

        let num_lines = num_panels.div_ceil(self.panels_per_line);
        let line_constraints = iter::repeat_n(Constraint::Fill(1), num_lines);

        let lines = Layout::default()
            .direction(line_direction)
            .constraints(line_constraints)
            .split(container);

        let mut panel_areas = Vec::with_capacity(num_panels);
        let panel_constraints = iter::repeat_n(Constraint::Fill(1), self.panels_per_line);

        for &line_area in lines.iter() {
            let panels_in_line = Layout::default()
                .direction(self.primary_direction)
                .constraints(panel_constraints.clone())
                .split(line_area);

            panel_areas.extend_from_slice(&panels_in_line);
        }

        panel_areas.truncate(num_panels);
        panel_areas
    }
}

#[derive(Clone, Copy, Debug)]
pub enum GridLayout {
    Rows(usize),
    Columns(usize),
}

impl Default for GridLayout {
    fn default() -> Self {
        GridLayout::Rows(3)
    }
}

impl From<GridLayout> for LayoutConfig {
    fn from(layout: GridLayout) -> Self {
        match layout {
            GridLayout::Rows(n) => LayoutConfig {
                primary_direction: Direction::Vertical,
                panels_per_line: n.max(1),
            },
            GridLayout::Columns(n) => LayoutConfig {
                primary_direction: Direction::Horizontal,
                panels_per_line: n.max(1),
            },
        }
    }
}
