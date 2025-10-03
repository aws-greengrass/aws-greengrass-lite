ARG BASE_IMAGE=ubuntu:22.04
FROM ${BASE_IMAGE}
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        systemd systemd-sysv dbus ca-certificates \
        zip unzip sudo

CMD ["/lib/systemd/systemd"]
