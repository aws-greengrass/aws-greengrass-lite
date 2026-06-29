## tracing spec

See [tracing design](../../design/tracing.md) for architecture and usage
guidance.

The tracing interface provides cross-daemon request correlation for log output.
When enabled, every log line emitted during a traced request carries a
structured bracket linking it to a shared trace_id. The implementation lives in
gg-sdk and is consumed by Greengrass nucleus lite daemons at core-bus
boundaries.

- [tracing-1] Tracing is gated behind a build-time flag. When off, all trace API
  expands to no-ops and log output is byte-identical to a non-traced build.
- [tracing-2] Trace context is stored in thread-local storage (TLS). Each thread
  carries an independent trace_id, span_id, and parent_span_id.
- [tracing-3] IDs are 16-bit unsigned integers rendered as 4-digit uppercase hex
  in log output. A missing or cleared value renders as `----`.
- [tracing-4] IDs are generated via `gg_rand_fill` and carry a non-zero
  invariant.
- [tracing-5] The log bracket format is
  `[trace_id:span_id:parent_span_id:file:line]`. It appears after the daemon tag
  and before the message on traced lines only.
- [tracing-6] When no trace is active, log lines have no bracket and are
  byte-identical to output from a non-traced build.
- [tracing-7] Trace context propagates across core-bus via three EventStream
  headers: `T` (trace_id), `S` (span_id), `P` (parent_span_id), each of type
  `EVENTSTREAM_INT32` with 16-bit values zero-extended to 32-bit.
- [tracing-8] A receiving daemon generates a fresh span_id and sets
  parent_span_id to the caller's span_id.
- [tracing-9] Mixed-version compatibility: a daemon built without tracing
  ignores inbound trace headers and omits them on outbound calls without error.

## Compilation Macros

### `GG_LOG_TRACE`

- [tracing-macros-1] CMake option `-DGG_LOG_TRACE=ON|OFF` (default OFF).
  Propagates to gg-sdk via `add_subdirectory`.
- [tracing-macros-2] When ON, defines `GG_TRACE_ENABLED` for gg-sdk translation
  units and enables all trace API and log bracket formatting.
- [tracing-macros-3] When OFF, `GG_TRACE_SCOPE_GUARD`, `GG_TRACE_ROOT_SCOPE`,
  and `GG_TRACE_INHERIT_SCOPE` expand to empty statements. No trace symbols
  appear in the resulting binary.

## Environment Variables

(None. Tracing is controlled exclusively at build time.)

## API

### TLS Accessors (gg/log.h)

- [tracing-api-1]
  `void gg_log_set_trace(uint16_t trace_id, uint16_t span_id, uint16_t parent_span_id)` -
  sets the calling thread's trace context.
- [tracing-api-2] `void gg_log_clear_trace(void)` - clears the calling thread's
  trace context.
- [tracing-api-3] `uint16_t gg_log_current_trace_id(void)` - returns the active
  trace_id, or 0 if no trace is set.
- [tracing-api-4]
  `void gg_log_get_trace(uint16_t *trace_id, uint16_t *span_id, uint16_t *parent_span_id)` -
  reads the full trace context.

### Orchestration (priv_include/gg/trace.h)

- [tracing-api-5]
  `void gg_trace_root_begin(const char *kind, const char *fmt, ...)` -
  idempotent. If a trace is already active, no-op. Otherwise generates fresh
  trace_id and span_id, sets TLS with parent_span_id = 0, emits one INFO
  `trace_start: <kind> ...` log line.
- [tracing-api-6]
  `size_t gg_trace_attach_headers(EventStreamHeader *headers, size_t headers_capacity)` -
  writes T/S/P headers into the provided array. Returns 3 on success, 0 if no
  trace is active or headers_capacity < 3.
- [tracing-api-7]
  `bool gg_trace_extract_and_apply(EventStreamHeaderIter headers)` - iterates
  inbound headers for T and S entries (type-guarded to INT32). Generates a fresh
  span_id, sets TLS to (T, fresh_span, S). Returns true if trace context was
  found and applied; false if no T header present (TLS unchanged).

### Scope Macros (priv_include/gg/trace.h)

- [tracing-api-8] `GG_TRACE_SCOPE_GUARD()` - declares a variable with
  `__attribute__((cleanup))` that calls `gg_log_clear_trace` on scope exit.
- [tracing-api-9] `GG_TRACE_ROOT_SCOPE(kind, fmt, ...)` - defensive clear +
  `gg_trace_root_begin(...)` + `GG_TRACE_SCOPE_GUARD()`. Self-gates: expands to
  nothing when `GG_TRACE_ENABLED` is not defined.
- [tracing-api-10] `GG_TRACE_INHERIT_SCOPE(headers)` - defensive clear +
  `gg_trace_extract_and_apply(headers)` + `GG_TRACE_SCOPE_GUARD()`. Self-gates:
  expands to nothing when `GG_TRACE_ENABLED` is not defined.
- [tracing-api-11] Scope macros declare local variables and must appear inside a
  brace block. Mirrors the `GG_MTX_SCOPE_GUARD` idiom.
