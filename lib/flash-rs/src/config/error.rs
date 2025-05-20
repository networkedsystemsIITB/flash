pub(super) type ConfigResult<T> = Result<T, ConfigError>;

#[derive(Debug, thiserror::Error)]
pub enum ConfigError {
    #[error("config error: idle timeout must be greater than 0")]
    InvalidIdleTimeout,
}
