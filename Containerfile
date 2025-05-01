FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive

CMD ["/lib/systemd/systemd"]

RUN apt-get update \
  && apt-get -y install --no-install-recommends \
    systemd systemd-sysv dbus ca-certificates sudo nano bash-completion \
    cgroup-tools build-essential pkg-config cmake git curl gdb python3-venv \
    libssl-dev libcurl4-openssl-dev libsqlite3-dev sqlite3 libyaml-dev \
    libsystemd-dev liburiparser-dev uuid-dev libevent-dev libzip-dev \
  && apt-get clean

COPY misc/container/getty-override.conf \
  /etc/systemd/system/console-getty.service.d/override.conf

RUN groupadd gg_component && useradd -Ng gg_component gg_component

RUN groupadd ggcore && useradd -Ng ggcore ggcore

RUN mkdir -p /etc/greengrass/config.d && mkdir -p /var/lib/greengrass \
  && chown ggcore:ggcore /var/lib/greengrass

COPY misc/container/01defaults.yaml /etc/greengrass/config.d/01defaults.yaml

RUN --mount=type=bind,target=/tmp/aws-greengrass-lite \
  --mount=type=cache,sharing=locked,target=/tmp/build \
  cd /tmp/aws-greengrass-lite \
  && cmake -B /tmp/build -D CMAKE_INSTALL_PREFIX=/usr -D GGL_LOG_LEVEL=DEBUG \
  && make -j -C /tmp/build install

RUN systemctl enable \
  greengrass-lite.target \
  ggl.aws_iot_tes.socket \
  ggl.aws_iot_mqtt.socket \
  ggl.gg_config.socket \
  ggl.gg_health.socket \
  ggl.gg_fleet_status.socket \
  ggl.gg_deployment.socket \
  ggl.gg_pubsub.socket \
  ggl.ipc_component.socket \
  ggl.gg-ipc.socket.socket \
  ggl.core.ggconfigd.service \
  ggl.core.iotcored.service \
  ggl.core.tesd.service \
  ggl.core.ggdeploymentd.service \
  ggl.core.gg-fleet-statusd.service \
  ggl.core.ggpubsubd.service \
  ggl.core.gghealthd.service \
  ggl.core.ggipcd.service \
  ggl.aws.greengrass.TokenExchangeService.service
