# finkit.core Module

Core utilities providing path expansion and logging setup for the finkit library.

## Overview

The `finkit.core` module contains foundational utilities used across all other modules:
- Path expansion with tilde (`~`) support
- Logging level configuration via spdlog

## Key Exports

### Path Utilities

```cpp
auto expand_path(fs::path p) -> fs::path;
```

Expands paths starting with `~` to the user's home directory. Returns the path
unchanged if it does not start with tilde.

### Logging

```cpp
enum class LogLevel { Trace, Debug, Info, Warn, Error };

void set_log_level(LogLevel level);
void set_log_level(string_view level);
```

Configure the global logging level. The string overload accepts: `"trace"`,
`"debug"`, `"info"`, `"warn"`, `"error"`. Defaults to `Info` if unrecognized.

## Dependencies

- `spdlog` - Logging library
- `std::filesystem` - Path handling

## Usage

```cpp
import finkit.core;

// Expand user paths
auto config_path = finkit::core::expand_path("~/.finkit/config.toml");

// Set logging verbosity
finkit::core::set_log_level(finkit::core::LogLevel::Debug);
// or
finkit::core::set_log_level("debug");
```

## Related

- [data.md](data.md) - Uses `expand_path` for config/database paths
- [Architecture](../architecture.md) - Module structure overview
