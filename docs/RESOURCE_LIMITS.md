# Resource limits

This page documents the measured RAM, disk, and CPU footprint of AWS Greengrass
Nucleus Lite (GGLite) across the three supported Linux architectures, and gives
recommended minimum device specifications for production deployments.

If you are choosing a device for a Greengrass Lite deployment, the
[recommended minimums](#recommended-minimum-device-specs) table below is the
short answer.

## Recommended minimum device specs

Use these as the minimum device envelope when planning a Greengrass Lite
deployment. **The values below cover Greengrass Lite itself only and do not
include headroom for customer components.** Add the resource budget of your own
components on top.

| Resource    | Recommended minimum     | Notes                                             |
| :---------- | :---------------------- | :------------------------------------------------ |
| RAM         | **15 MB** free          | GGLite daemons at steady state — components extra |
| Disk (free) | **5 MB** free           | GGLite install + runtime state — components extra |
| CPU         | Any supported Linux CPU | armv7l, aarch64, and x86_64 are all supported     |
| OS          | Linux with systemd      | Ubuntu 22.04+ and Raspberry Pi OS 12+ validated   |

These numbers are the same across x86_64, aarch64, and armv7l. They cover the
GGLite daemons at realistic-load steady state. See
[Measured footprint](#measured-footprint) below for the per-architecture
measurements that back these numbers, and
[Deployment-time peaks](#deployment-time-peaks-cloud-deployment) for the
behavior during a cloud deployment.

## Measured footprint

All numbers below were measured against the official `aws-greengrass-lite`
v2.5.0 prebuilt `.deb` packages (`MinSizeRel` build), sampling at 10-second
intervals over a 10-minute steady-state window after a 5-minute warmup. Nine
long-running daemons are included in the PSS total: `ggconfigd`,
`ggdeploymentd`, `gghealthd`, `ggipcd`, `ggpubsubd`, `iotcored`, `tesd`,
`tes-serverd`, `gg-fleet-statusd`. (`recipe-runner` is ephemeral and is not
included in the steady-state total.)

See [`benchmark/REPORT.md`](../benchmark/REPORT.md) for the full methodology,
raw per-daemon breakdowns, and reproducibility notes.

### RAM (PSS) and CPU

PSS (Proportional Set Size) is the headline RAM metric because GGLite is a
multi-daemon system that shares libc / libssl / libcurl across processes; PSS
attributes shared pages fairly and is the only metric that does not double-count
them. RSS and VSS are reported in `benchmark/REPORT.md` for capacity-planning
upper bounds.

| Architecture | Device         | Baseline PSS | Simple workload PSS | Realistic-load PSS | CPU at realistic load |
| :----------- | :------------- | :----------- | :------------------ | :----------------- | :-------------------- |
| x86_64       | EC2 t3.small   | 14.6 MB      | 14.7 MB             | **14.8 MB**        | 7.5% user / 83% idle  |
| aarch64      | Raspberry Pi 4 | 14.6 MB      | 15.7 MB             | **15.8 MB**        | 3.1% user / 90% idle  |
| armv7l       | Raspberry Pi 3 | 12.3 MB      | 12.3 MB             | **12.4 MB**        | 11% user / 76% idle   |

**Workload definitions:**

- **Baseline:** GGLite daemons running with no deployed components.
- **Simple workload:** `hello-world` + `ipc-publisher` + `ipc-subscriber` at 1
  IPC message/sec.
- **Realistic load:** 2× `ipc-publisher` + 2× `ipc-subscriber` +
  `iot-core-publisher` + `s3-uploader` — 2 IPC messages/sec local, 1 MQTT
  message/sec to IoT Core, 1 S3 upload/min.

### Deployment-time peaks (cloud deployment)

When GGLite receives a cloud deployment (the customer-facing path via AWS IoT
Greengrass V2), memory and CPU spike above the steady-state values shown above
during the ~1–2 minute deployment window. The numbers below are the peaks
observed during that window; once components are running, usage returns to the
steady-state levels shown above. **The peak PSS during a cloud deployment is
within ~1 MB of the steady-state value across all three architectures**, so the
15 MB recommended minimum covers both the steady-state and deployment windows
for GGLite itself.

| Architecture      | Peak PSS | Peak CPU | Bandwidth (rx+tx per deploy) | Disk write (per deploy) | Typical deployment duration |
| :---------------- | :------- | :------- | :--------------------------- | :---------------------- | :-------------------------- |
| x86_64 (t3.small) | 15.6 MB  | 100%     | ~260 KB                      | ~47 MB                  | ~67 s                       |
| aarch64 (RPi 4)   | 15.2 MB  | ~87%     | ~1–3 MB                      | ~30 MB                  | 40–63 s                     |
| armv7l (RPi 3)    | 13.2 MB  | 95%      | ~2–4 MB                      | ~24 MB                  | 17–254 s                    |

Deployment duration depends on S3 region, network connectivity, and device
CPU/storage — the values above are representative and were measured from
us-west-2 S3 to each device. See `benchmark/REPORT.md` for full methodology and
per-run data.

### Disk

**Install size** is the footprint of the installed `.deb` package
(`dpkg -L aws-greengrass-lite | xargs du -cb`).

| Architecture | Install size |
| :----------- | :----------- |
| x86_64       | 1.10 MB      |
| aarch64      | 1.09 MB      |
| armv7l       | 0.83 MB      |

**Runtime size** is `/var/lib/greengrass` — the GGLite state directory. On a
fresh install it is approximately 250–400 KB across all architectures, and grows
to approximately 400–500 KB after deploying the four-component realistic-load
scenario. Runtime size grows further with the number of components you deploy
and the size of their artifacts; size your disk with this in mind.

### Startup time

Time from `systemctl restart greengrass-lite.target` to all nine long-running
services reporting `active`.

| Architecture | Startup time                                                                          |
| :----------- | :------------------------------------------------------------------------------------ |
| x86_64       | 1,328 ms (warm boot)                                                                  |
| aarch64      | 1,700 ms                                                                              |
| armv7l       | ~2–3 s (core daemons; full target may be longer if stale component units are present) |

## Architecture support

| Architecture | Package                                | Status                          |
| :----------- | :------------------------------------- | :------------------------------ |
| x86_64       | `aws-greengrass-lite-v2.5.0_amd64.deb` | Supported                       |
| aarch64      | `aws-greengrass-lite-v2.5.0_arm64.deb` | Supported                       |
| armv7l       | `aws-greengrass-lite-v2.5.0_armhf.deb` | Supported                       |
| riscv64      | —                                      | Experimental (not fully tested) |

armv7l covers 32-bit Raspberry Pi devices (RPi 2B, RPi 3 running a 32-bit OS)
and other Armv7 Linux boards.

## The 15 MB RAM footprint target

The Greengrass Lite architecture document (`docs/design/gg-architecture.md`)
originally targeted a memory footprint below 10 MB. Based on the measurements in
this document, the requirement has been revised to **15 MB** so that the target
reflects the actual achievable footprint across all supported architectures with
realistic component workloads.

**Definition:** The 15 MB RAM footprint target is defined as the **median PSS
(Proportional Set Size) summed across all long-running GGLite daemons**
(`ggconfigd`, `ggdeploymentd`, `gghealthd`, `ggipcd`, `ggpubsubd`, `iotcored`,
`tesd`, `tes-serverd`, `gg-fleet-statusd`) **at realistic-load steady state**,
measured over a 10-minute window after a 5-minute warmup, sampled every 10
seconds.

**Current status:** All supported architectures stay within the 15 MB target.
Measured realistic-load PSS ranges from 12.4 MB (armv7l) to 15.8 MB (aarch64).
For device sizing, use the
[recommended minimums](#recommended-minimum-device-specs).

## Reproducing these measurements

The benchmark harness lives in [`benchmark/`](../benchmark/) in this repo and
runs end-to-end on any of the three supported architectures:

```bash
cd benchmark
./scripts/run-all.sh <arch>   # arch = x86_64 | aarch64 | armv7l
```

See [`benchmark/REPORT.md`](../benchmark/REPORT.md) for the full methodology
(sampling discipline, per-daemon breakdowns, smoke-test gate, tooling list).
