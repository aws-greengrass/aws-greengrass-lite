# logging spec

The logging interface provides a consistent logging API for all core components.
This enables the logging implementation to be swapped out at compile time if a
customer's usecase needs alternative handling for logging. The default
implementation of the interface logs to stderr.

- [logging-1] The maximum logging level can be configured at compile time in
  order to reduce binary size.
- [logging-2] The logging functionality can be swapped out at compile time by
  providing a non-weak definition for the logging implementation function.
- [logging-3] The logging functionality can be swapped out at runtime though
  LD_PRELOAD on systems that support that mechanism.
- [logging-4] The default logging implementation logs all compile-time enabled
  log levels to stdout.
- [logging-5] Logging macro usages report syntax and usage warnings even when
  the logging level is disabled at compile time.

## Compilation Macros

### `GGL_LOG_LEVEL`

- [logging-macros-1] `GGL_LOG_LEVEL` can be set to one of the logging level
  macro names in order to control the compile time maximum logging level. This
  ensures log messages for other logging levels can be optimized out by the
  compiler.

## Environment Variables

## API

- [logging-api-1] The following logging levels are defined:
  - `GGL_LOG_NONE` (0)
  - `GGL_LOG_ERROR` (1)
  - `GGL_LOG_WARN` (2)
  - `GGL_LOG_INFO` (3)
  - `GGL_LOG_DEBUG` (4)
  - `GGL_LOG_TRACE` (5)
- [logging-api-2] The following macros are available for logging at various
  levels:
  - `GGL_LOGE`
  - `GGL_LOGW`
  - `GGL_LOGI`
  - `GGL_LOGD`
  - `GGL_LOGT`
- [logging-api-3] The logging macros take a tag string, and then printf style
  args (format string followed by matching arguments).
- [logging-api-4] The logging macros pass compile-time enabled log levels to a
  function with the following signature:

  ```c
  void ggl_log(
    uint32_t level,
    const char *file,
    int line,
    const char *tag,
    const char *format,
    ...
  )
  ```

  This includes the log level of the log, a string with file information (may be
  file name or full file path), the line number in the file, the provided tag,
  and the provided printf style arguments.

- [logging-api-5] The provided logging implementation is a weak definition to
  allow overriding.
