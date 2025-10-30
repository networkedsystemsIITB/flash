use std::{
    io,
    sync::Arc,
    time::{Duration, Instant},
};

use ratatui::{
    Terminal,
    crossterm::{
        ExecutableCommand,
        event::{self, Event, KeyCode, KeyEvent, KeyEventKind},
        terminal::{self, EnterAlternateScreen, LeaveAlternateScreen},
    },
    prelude::CrosstermBackend,
};

use crate::stats::Stats;

use super::{
    error::{TuiError, TuiResult},
    layout::{GridLayout, LayoutCache},
    panel::StatsPanel,
};

#[derive(Debug)]
pub struct StatsDashboard {
    frame_interval: Duration,
    last_frame_time: Instant,
    terminal: Terminal<CrosstermBackend<io::Stdout>>,
    panels: Vec<StatsPanel>,
    layout_cache: LayoutCache,
}

impl StatsDashboard {
    #[allow(clippy::missing_errors_doc)]
    pub fn new(
        stats: impl Iterator<Item = Arc<Stats>>,
        fps: u64,
        layout: GridLayout,
    ) -> TuiResult<Self> {
        let panels = stats
            .enumerate()
            .map(|(i, stat)| StatsPanel::new(i, stat))
            .collect::<Vec<_>>();

        let num_panels = panels.len();
        if num_panels == 0 {
            return Err(TuiError::EmptyStats);
        }

        let terminal = Terminal::new(CrosstermBackend::new(io::stdout()))?;

        Ok(Self {
            frame_interval: Duration::from_micros(1_000_000 / fps.max(1)),
            last_frame_time: Instant::now(),
            terminal,
            panels,
            layout_cache: LayoutCache::new(layout, num_panels),
        })
    }

    fn resize_panels(&mut self, terminal_size: (u16, u16)) {
        if let Some(panel_areas) = self.layout_cache.update_panel_areas(terminal_size) {
            for (panel, &area) in self.panels.iter_mut().zip(panel_areas.iter()) {
                panel.resize(area);
            }
        }
    }

    fn render(&mut self) -> io::Result<()> {
        self.terminal.draw(|frame| {
            for panel in &mut self.panels {
                panel.render(frame);
            }
        })?;

        Ok(())
    }

    fn poll_until_next_frame(&mut self) -> io::Result<bool> {
        let next_frame_time = self.last_frame_time + self.frame_interval;

        loop {
            let now = Instant::now();
            if now >= next_frame_time {
                break;
            }

            if event::poll(next_frame_time - now)? {
                match event::read()? {
                    Event::Key(KeyEvent {
                        code,
                        kind: KeyEventKind::Press,
                        ..
                    }) => match code {
                        KeyCode::Char('q' | 'Q') | KeyCode::Esc => return Ok(true),
                        KeyCode::Char('r' | 'R') => return Ok(false),
                        _ => {}
                    },
                    Event::Resize(width, height) => {
                        self.resize_panels((width, height));
                        return Ok(false);
                    }
                    _ => {}
                }
            }
        }

        Ok(false)
    }

    #[allow(clippy::missing_errors_doc)]
    pub fn run(&mut self) -> TuiResult<()> {
        let _guard = TerminalGuard::new(&mut self.terminal)?;
        self.resize_panels(terminal::size()?);

        loop {
            self.last_frame_time = Instant::now();
            self.render()?;

            if self.poll_until_next_frame()? {
                break;
            }
        }

        Ok(())
    }
}

struct TerminalGuard;

impl TerminalGuard {
    fn new(terminal: &mut Terminal<CrosstermBackend<io::Stdout>>) -> io::Result<Self> {
        terminal::enable_raw_mode()?;
        io::stdout().execute(EnterAlternateScreen)?;
        terminal.hide_cursor()?;
        Ok(Self)
    }
}

impl Drop for TerminalGuard {
    fn drop(&mut self) {
        let _ = io::stdout().execute(LeaveAlternateScreen);
        let _ = terminal::disable_raw_mode();
        // terminal.show_cursor().ok();
    }
}
