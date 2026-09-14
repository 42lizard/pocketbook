FROM debian:trixie-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake python3 \
    qt6-base-dev qt6-declarative-dev \
    libssl-dev libxml2-dev libsqlite3-dev libjson-c-dev \
    libcurl4-openssl-dev \
    qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtqml-workerscript \
    fonts-dejavu-core novnc websockify \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
CMD ["sh", "tools/pocketbook-simulator/run.sh"]
