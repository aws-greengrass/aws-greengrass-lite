# `ggl-policy-validation` design

See the deployment-time requirement
[`[ggdeploymentd-3.3]`](../spec/executable/ggdeploymentd.md) for the public
contract.

## Background

Greengrass component `accessControl` policies use a small grammar for resource
strings (see the public docs for
[IPC authorization policies](https://docs.aws.amazon.com/greengrass/v2/developerguide/interprocess-communication.html#ipc-authorization-policies)
for the user-facing description):

- `*` is a wildcard that can appear anywhere and matches any sequence of
  characters.
- `${*}`, `${?}`, and `${$}` are escapes that produce literal `*`, `?`, and `$`
  characters in the matched topic.

Anything else inside a `${...}` escape is undefined; in particular, a bare `?`
was previously accepted by Greengrass nucleus lite and silently honored as a
literal `?` character in the topic. AWS IoT Core does not allow `?` in topic
names, so a policy or request topic containing `?` is effectively inert (it can
never match a real cloud topic) and almost always indicates a mistake by the
recipe author.

This module rejects malformed resource strings up front, both at deployment time
(in `ggdeploymentd`) and at IPC call time (in `ggipcd`).

## Design decisions

### 1. Be stricter than Greengrass v2

Greengrass V2 only logs and skips malformed `accessControl` policies; the
deployment still succeeds. We chose to **fail** the deployment in Greengrass
nucleus lite instead.

The reasons it is acceptable to break parity here:

- **Migration path is already manual.** Customers moving from Greengrass nucleus
  to Greengrass nucleus lite inspect and adjust their component recipes anyway
  as part of migration, so a deploy-time error pointing at the offending field
  is strictly more helpful than the silent v2 behavior.

### 2. Fail the deployment, don't silently filter

When `ggdeploymentd` detects a malformed `accessControl` resource it **fails the
entire deployment** and logs the offending component and resource string.
Failing fast surfaces the error directly to the user instead of letting the
component start with a policy that can never grant access. When cloud-side
reporting is added, the failure reason will also be surfaced through the
deployment status API.

### 3. Two enforcement points: deploy time **and** IPC call time

The validator is called from two places:

- `ggdeploymentd` — on every `accessControl` resource it finds in the recipe
  `DefaultConfiguration` and in the deployment doc `configurationUpdate.merge`,
  before writing the merged configuration to the config store.
- `ggipcd` — on the request resource (the topic being published or subscribed
  to) at the start of `ggl_ipc_auth`, before any policy lookup.

Both gates are needed because the rule applies symmetrically: a policy that
would never match anything useful is a mistake, and a request topic that AWS IoT
Core would reject is also a mistake. The deploy-time gate keeps malformed
configuration out of the config store; the call-time gate catches malformed
request topics that originate at runtime and never appeared in any deployment.

## Implementation

```
modules/
├── ggl-policy-validation/
│   ├── CMakeLists.txt
│   ├── include/ggl/policy_validation.h
│   │     └── GgError ggl_validate_policy_resource(GgBuffer resource);
│   └── src/policy_validation.c
│         ├── ggl_validate_policy_resource(...) — leaf validator
│         └── 12 inline tests guarded by `GG_SDK_TESTING`
├── ggdeploymentd/
│   └── src/access_control_validation.{c,h}
│         ├── validate_access_control_policies(GgObject *config_obj)
│         └── validate_merge_access_control(GglDeployment *, GgBuffer)
│         (walk deployment-doc shape; call ggl_validate_policy_resource
│         on each resource string)
└── ggipcd/
    └── src/ipc_authz.c
          └── ggl_ipc_auth(...) — calls ggl_validate_policy_resource(resource)
            before any policy lookup
```

The validator returns `GG_ERR_INVALID` for a malformed resource and logs the
offending string and position. At deploy time the call-site logs
`"Deployment rejected: invalid accessControl policy in component <name>."` and
the deployment is reported FAILED. At IPC call time the existing `ggl_ipc_auth`
non-OK handling produces an `UnauthorizedError` to the caller; the validator's
specific reason is in the ggipcd journal.
