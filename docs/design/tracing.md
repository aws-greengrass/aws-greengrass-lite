# Request Tracing Design

See [tracing spec](../spec/library/tracing.md) for the formal API specification.

## Overview

Request tracing correlates log lines emitted by cooperating daemons for a single
logical operation by attaching a shared trace_id to every line.

## Requirements

- Cross-daemon log correlation via shared trace_id.
- Zero call-site changes - existing GG_LOGI/D/W/E/T macros unchanged.
- Zero runtime cost when the build flag is off (compile-time stripped).
- Allocation-free, syscall-free, O(1) per traced log line.
- Mixed-version safe - untraced daemons are opaque but do not break the trace.

## Architecture

The tracing system spans two repositories:

1. **gg-sdk** owns the primitives: TLS storage, ID generation, log bracket
   formatting, header attach/extract, and scope macros
   (`priv_include/gg/trace.h`, `src/trace.c`).
2. **Greengrass nucleus lite** uses those primitives at daemon entry points
   (`GG_TRACE_ROOT_SCOPE`) and core-bus boundaries (`GG_TRACE_INHERIT_SCOPE` on
   server side, `gg_trace_attach_headers` on client side).

Tracing is gated by CMake option `-DGG_LOG_TRACE=ON`. When OFF (default), all
macros expand to nothing and binaries contain no trace symbols. See the spec for
the full API surface and IDs.

## Wire Format

Trace context rides on core-bus messages as three EventStream headers:

| Header | Type              | Content                                |
| ------ | ----------------- | -------------------------------------- |
| `T`    | EVENTSTREAM_INT32 | trace_id (16-bit, zero-extended)       |
| `S`    | EVENTSTREAM_INT32 | span_id (16-bit, zero-extended)        |
| `P`    | EVENTSTREAM_INT32 | parent_span_id (16-bit, zero-extended) |

When no trace is active, headers are omitted entirely.

## Root Entry Points

Three daemons create root traces:

| Daemon           | Entry Point                    | Kind                |
| ---------------- | ------------------------------ | ------------------- |
| ggipcd           | `handle_stream_operation`      | `ipc_request`       |
| ggdeploymentd    | `deployment_handler` (2 sites) | `deployment_jobs`   |
| gg-fleet-statusd | periodic tick                  | `fleet_status_tick` |

Each uses `GG_TRACE_ROOT_SCOPE(kind, fmt, ...)` which bundles a defensive clear,
`gg_trace_root_begin`, and a scope-guard auto-clear.

## Propagation

- **trace_id** is stable across all daemons handling one request.
- **span_id** is fresh per daemon (generated on entry).
- **parent_span_id** is the caller's span_id, chaining causality.

The span_id does not change within a single daemon; a new child span is created
only when crossing a daemon boundary via core-bus.

Sender (core-bus client in `ggl_call`): `gg_trace_attach_headers` reads TLS and
writes T/S/P into the outbound frame.

Receiver (core-bus server dispatch): `GG_TRACE_INHERIT_SCOPE(headers)` fires a
defensive clear, `gg_trace_extract_and_apply` (read T/S, generate fresh span,
set TLS with parent = caller's span), then a scope-guard that auto-clears on
scope exit.

## Mixed-Version Compatibility

A daemon built without `-DGG_LOG_TRACE` ignores inbound T/S/P headers, omits
them on outbound calls, and emits no trace brackets. Upstream and downstream
daemons retain their trace context unbroken; the untraced daemon appears as an
opaque hop.

## Operational Use

```
# All root traces in the last 2 minutes:
journalctl --since "2 min ago" --grep 'trace_start' --no-pager

# Follow one trace across all daemons:
journalctl --since "2 min ago" --grep '\[A34F:' --no-pager
```

The leading `[` and trailing `:` anchor the match to the trace bracket,
excluding incidental hex matches in message bodies.

## Future Work

- **Intra-daemon subspans** (`GG_TRACE_SUBSPAN()` macro) for finer-grained
  filtering within a single daemon
- CLI tree renderer for trace visualization.
