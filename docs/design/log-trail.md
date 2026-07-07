# Log Trail Design

See [log-trail spec](../spec/library/log-trail.md) for the formal API
specification.

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
   (`priv_include/gg/log-trail.h`, `src/log-trail.c`).
2. **Greengrass nucleus lite** uses those primitives at daemon entry points
   (`GG_LOG_TRAIL_ROOT_SCOPE`) and core-bus boundaries
   (`GG_LOG_TRAIL_INHERIT_SCOPE` on server side, `gg_log_trail_attach_headers`
   on client side).

Tracing is gated by CMake option `-DGG_LOG_TRAIL=ON`. When OFF (default), all
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

Each uses `GG_LOG_TRAIL_ROOT_SCOPE(kind, fmt, ...)` which bundles a defensive
clear, `gg_log_trail_root_begin`, and a scope-guard auto-clear.

## Propagation

- **trace_id** is stable across all daemons handling one request.
- **span_id** changes when crossing a daemon boundary via core-bus, and can also
  change within a single daemon when an explicit sub-span is opened (see
  Sub-spans below).
- **parent_span_id** is the caller's span_id, chaining causality.

By default, the span_id does not change within a single daemon; a new child span
is created only when crossing a daemon boundary via core-bus or when a call site
explicitly opens a sub-span.

Sender (core-bus client in `ggl_call`): `gg_log_trail_attach_headers` reads TLS
and writes T/S/P into the outbound frame.

Receiver (core-bus server dispatch): `GG_LOG_TRAIL_INHERIT_SCOPE(headers)` fires
a defensive clear, `gg_log_trail_extract_and_apply` (read T/S, generate fresh
span, set TLS with parent = caller's span), then a scope-guard that auto-clears
on scope exit.

## Sub-spans

Within a single daemon, `GG_LOG_TRAIL_SUBSPAN_SCOPE()` opens a child span inside
the currently active trace. It preserves `trace_id`, generates a fresh
`span_id`, and makes the caller's previous `span_id` the new `parent_span_id`.
On scope exit, the caller's (trace_id, span_id, parent_span_id) is restored (not
cleared) via a `cleanup` attribute, so any work following the sub-span continues
under the original context.

Sub-spans compose with cross-daemon propagation without special handling: if the
sub-span makes a core-bus call, `gg_log_trail_attach_headers` serializes
whatever is in TLS at that moment, so the receiving daemon sees the sub-span's
`span_id` as its parent. This lets one daemon fan out to multiple downstream
daemons under a single sub-span; every receiver correctly reports the same
parent, forming a proper tree.

The macro takes no arguments and uses `GG_MODULE` as the sub-span "kind" label,
which is logged once at DEBUG when the sub-span opens. The random `span_id` plus
the `file:line` in the log bracket disambiguate multiple sub-spans within one
daemon; explicit per-call-site naming is not provided.

When no trace is active on the calling thread, the macro is a no-op - it does
not manufacture a `trace_id` and it does not log the subspan_start line.

## Mixed-Version Compatibility

A daemon built without `-DGG_LOG_TRAIL` ignores inbound T/S/P headers, omits
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

- CLI tree renderer for trace visualization.
