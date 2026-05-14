# GGLite Resource Benchmark Harness

Reproducible benchmark harness for measuring AWS Greengrass Nucleus Lite (GGLite)
resource consumption across x86_64, aarch64, and armv7l architectures.

The harness covers two scenarios:

- **Phase 1 — Steady-state local deployment**: nucleus daemons measured at
  rest with components deployed via `ggl-cli deploy --recipe-dir` (local
  path, no cloud round-trip).
- **Phase 2 — Cloud deployment**: nucleus daemons measured during and after
  a cloud deployment via `aws greengrassv2 create-deployment` (the production
  customer path).

See [`REPORT.md`](./REPORT.md) for the latest measured numbers and full
methodology, and [`docs/RESOURCE_LIMITS.md`](../docs/RESOURCE_LIMITS.md) for
the customer-facing summary.

## Folder Layout

```
benchmark/
├── README.md                        # This file
├── REPORT.md                        # Detailed methodology + raw data per arch
├── scripts/
│   ├── cloud-setup.env.example      # Template for AWS resource env vars
│   ├── provision-account.sh         # One-time account setup (idempotent)
│   ├── provision-device.sh          # Install GGLite on a target device
│   ├── run-all.sh                   # Orchestrator: smoke → scenarios → report
│   ├── smoke-test.sh                # 6-test go/no-go gate
│   ├── measure.sh                   # Phase 1 concurrent PSS/RSS/CPU/startup sampling
│   ├── measure-deployment.sh        # Phase 2 1-sec deployment-window sampling
│   ├── pull-data.sh                 # Sync raw CSVs from device to host
│   ├── report-generator.sh          # CSV → Markdown tables
│   └── scenarios/
│       ├── baseline.sh              # Phase 1: no components
│       ├── simple-component.sh      # Phase 1: 3 components, low IPC
│       ├── realistic-load.sh        # Phase 1: 6 components, mixed workload
│       ├── cloud-deploy-initial.sh  # Phase 2: first-time cloud deployment
│       ├── cloud-deploy-update.sh   # Phase 2: version-bump deployment
│       └── deploy-helpers.sh        # Shared deploy / undeploy helpers
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

The harness has a clear host / device split:

- **Host**: where you orchestrate from (your dev machine). The host runs
  `provision-account.sh`, `provision-device.sh`, and `pull-data.sh`. It does
  not run measurements itself.
- **Device**: the target you are benchmarking (RPi, EC2 instance, etc.). The
  device runs `run-all.sh` and the underlying samplers. Measurements happen
  here.

### 1. One-time AWS account setup (host)

Run `provision-account.sh` once per AWS account to set up the IAM role,
IoT policy, IoT Thing Groups, and S3 bucket used by the benchmark. The
script is idempotent — safe to re-run.

```bash
cd benchmark/scripts
./provision-account.sh
```

Then copy the env template and fill in your AWS resource values:

```bash
cp cloud-setup.env.example cloud-setup.env
# Edit cloud-setup.env with your IoT Thing names, S3 bucket, etc.
```

### 2. Provision a target device (host)

Installs GGLite on the device and writes its certificates / config:

```bash
./provision-device.sh <device-ip> <arch>
# arch: x86_64 | aarch64 | armv7l
```

### 3. Copy the `benchmark/` folder onto the device

`run-all.sh` and the samplers run on the device, so the device needs a copy
of this folder. Either clone the repo on the device, or sync from the host:

```bash
# from the host, with the repo checked out:
rsync -avz benchmark/ <user>@<device-ip>:~/benchmark/
```

### 4. Run the benchmark (on the device)

SSH to the device and run:

```bash
ssh <user>@<device-ip>
cd ~/benchmark/scripts
sudo ./run-all.sh <arch>             # Phase 1 only (steady-state local deploy)
sudo ./run-all.sh <arch> --phase2    # Phase 1 + Phase 2 (adds cloud-deploy scenarios)
```

Output lands on the **device** under `~/benchmark/data/<arch>/<YYYY-MM-DD>/`
(or wherever the benchmark folder lives on the device).

### 5. Pull raw data back to the host (host)

From the host, sync the device's `data/<arch>/` directory back into this
repo's `benchmark/data/<arch>/`:

```bash
./pull-data.sh <arch> <user>@<device-ip>
# Optional 3rd arg: remote benchmark/data path (default: ~/benchmark/data)
```

The host-side `benchmark/data/` directory is `.gitignored` but kept locally
so runs are auditable and re-analyzable. The orchestrator on the device
appends a dated section to its on-device `REPORT.md`; the consolidated
`REPORT.md` in this repo summarizes results across runs.

## Prerequisites

**Local machine** (where you run the orchestration scripts):

- `ssh` and `scp`
- `curl`
- `unzip` (to extract the `.deb` from the GitHub release archive)
- `awscli` (for `provision-account.sh` and Phase 2 cloud-deploy scenarios)

**Target device**:

- Ubuntu 22.04+ (or Raspberry Pi OS 12+)
- `systemd` as PID 1
- SSH access (key-based recommended)
- Internet access (for `apt-get` during provisioning)
- Tools installed by the provisioning script: `smem`, `cgroup-tools`,
  `awscli` (Phase 2 only)

**AWS resources** (see `cloud-setup.env.example`): IoT Thing(s), device
certificate, IoT policy, IAM role, role alias, and an S3 bucket — all set
up by `provision-account.sh`.

## Workload Scenarios

| Scenario              | Components                                                                      | Workload                                       |
| :-------------------- | :------------------------------------------------------------------------------ | :--------------------------------------------- |
| Phase 1: baseline     | None                                                                            | Idle nucleus                                    |
| Phase 1: simple       | `hello-world` + `ipc-publisher` + `ipc-subscriber`                              | 1 IPC msg/sec local                             |
| Phase 1: realistic    | 2× publisher + 2× subscriber + `iot-core-publisher` + `s3-uploader`             | 2 msg/sec IPC + 1 msg/sec IoT Core + 1 S3/min   |
| Phase 2: cloud-initial| Same as realistic-load, deployed via `aws greengrassv2 create-deployment`       | First-time cloud deployment + 10-min steady    |
| Phase 2: cloud-update | Version-bump (`hello-world` 1.0.0 → 1.0.1) on an already-deployed core device   | Update deployment + 10-min steady              |

## Non-Goals

The harness is intentionally scoped to in-process resource measurement.
The following are **out of scope**:

- **CI integration for automated regression detection.** The harness runs
  on demand; wiring it into a scheduled CI workflow is a separate effort.
- **Network degradation testing.** Measuring GGLite behavior under flaky
  MQTT / TES connectivity is out of scope (Phase 2 measures network
  *utilization*, not degradation).
- **Flash wear and IOPS.** Constrained devices can fail from write
  amplification on embedded flash; not measured here.
- **riscv64.** Architecture is experimental in GGLite and not part of the
  primary benchmark matrix.
- **Non-Lite 1P components** (Stream Manager, Log Manager, Secret Manager).
  These are Java-based and owned by a different team; the harness scopes
  to GGLite daemons only.
- **Concurrent-deployment stress testing.** Unusual failure mode.
- **`RelWithDebInfo` build comparison.** Only `MinSizeRel` is measured; the
  harness is build-type-agnostic so this can be repeated later.
- **Unit tests for the harness scripts.** The harness itself is integration
  test coverage; `shellcheck` lint provides syntax-level enforcement.
- **24-hour soak.** Long-run leak detection is tracked separately as a
  follow-up appendix to `REPORT.md`.
- **Power consumption.** Hardware-dependent; documented as future work.

## Results

See [`REPORT.md`](./REPORT.md) for the full detailed report with per-arch
data, per-daemon breakdowns, deployment-time peaks, post-deploy steady-state
tables, and methodology details.

The customer-facing summary lives at
[`docs/RESOURCE_LIMITS.md`](../docs/RESOURCE_LIMITS.md).
