# GGLite Resource Benchmark Harness

Reproducible benchmark harness for measuring AWS Greengrass Nucleus Lite
resource consumption across x86_64, aarch64, and armv7l architectures.

This work addresses ORR observation M1 ("Resource limits documented only for
x86_64"). Full methodology and rationale are in
[`.kiro/steering/agents/m1-resource-limits-prd.md`](../.kiro/steering/agents/m1-resource-limits-prd.md).

## Folder Layout

```
benchmark/
├── README.md                        # This file
├── REPORT.md                        # Full methodology + raw data (created by Slice 5)
├── scripts/
│   ├── cloud-setup.env.example      # Template for AWS resource env vars
│   ├── provision-device.sh          # Install GGLite on a target device
│   ├── run-all.sh                   # Orchestrator: smoke → scenarios → report
│   ├── smoke-test.sh                # 6-test go/no-go gate
│   ├── measure.sh                   # Concurrent PSS/RSS/CPU/startup sampling
│   ├── report-generator.sh          # CSV → Markdown tables
│   └── scenarios/
│       ├── baseline.sh
│       ├── simple-component.sh
│       └── realistic-load.sh
├── components/                      # Frozen copies of example components
│   ├── hello-world/
│   ├── ipc-publisher/
│   ├── ipc-subscriber/
│   ├── iot-core-publisher/
│   └── s3-uploader/
└── data/                            # Per-run raw output (.gitignored)
    ├── x86_64/<YYYY-MM-DD>/
    ├── aarch64/<YYYY-MM-DD>/
    └── armv7l/<YYYY-MM-DD>/
```

## Quick Start

1. Copy the env template and fill in your AWS resource values:

   ```bash
   cd benchmark/scripts
   cp cloud-setup.env.example cloud-setup.env
   # Edit cloud-setup.env with your IoT Thing, certs, endpoints, etc.
   ```

2. Provision a target device:

   ```bash
   ./provision-device.sh <device-ip> <arch>
   # arch: x86_64 | aarch64 | armv7l
   ```

3. Run the full benchmark suite (after all scripts are implemented):

   ```bash
   ./run-all.sh <arch>
   ```

## Prerequisites

**Local machine** (where you run the script):
- `ssh` and `scp`
- `curl`
- `unzip` (to extract the .deb from the GitHub release zip)
- `awscli` (for cloud-setup only, not needed by provision-device.sh itself)

**Target device**:
- Ubuntu 22.04+ (or 24.04 for EC2)
- SSH access (key-based recommended)
- Internet access (for apt-get during provisioning)
- Tools installed by the script: `smem`, `sysstat` (for `mpstat`), `cgroup-tools`, `systemd`

**AWS resources** (see `cloud-setup.env.example`):
- IoT Thing, certificate, and role alias provisioned

## Non-Goals

See the PRD for the full list. Key items not covered by this harness:

- CI integration for automated regression detection
- Network degradation testing
- Flash wear / IOPS measurement
- riscv64 architecture
- Non-Lite 1P components (Stream Manager, Log Manager, Secret Manager)
- `RelWithDebInfo` build type comparison
- Unit tests for the benchmark harness scripts

## Notes

### config.d posixUser override

The `.deb` postinst creates `/etc/greengrass/config.d/greengrass-lite.yaml` with
`posixUser: "ggcore:ggcore"`. This config.d file likely takes precedence over the
main `/etc/greengrass/config.yaml` setting of `posixUser: "gg_component:gg_component"`.

**Impact**: Components will run as `ggcore` unless the config.d file is removed or
a benchmark-specific `posixUser` is set per-component in the recipe.

This is acceptable for Slice 0 (provisioning validation) but Slice 1 benchmark
scenarios should be aware when measuring per-component memory attribution.

## Results

See [`REPORT.md`](./REPORT.md) for the full internal report with per-arch data,
per-daemon breakdowns, and methodology details (created after Slices 1–3
complete).

The customer-facing summary lives at [`docs/RESOURCE_LIMITS.md`](../docs/RESOURCE_LIMITS.md)
(created by Slice 4).
