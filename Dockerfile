FROM espressif/idf:release-v5.5

ARG DEBIAN_FRONTEND=noninteractive

# Install tools required by firmware + UI + release automation.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        git-lfs \
        gnupg \
        jq \
        python3-pip \
        unzip \
        xz-utils \
        zip \
    && rm -rf /var/lib/apt/lists/*

# Use Node 16 for legacy webapp packages (node-sass toolchain compatibility).
RUN curl -fsSL https://deb.nodesource.com/setup_16.x | bash - \
    && apt-get update \
    && apt-get install -y --no-install-recommends nodejs \
    && npm install -g npm@8 \
    && rm -rf /var/lib/apt/lists/*

RUN python -m pip install --no-cache-dir setuptools pygit2 requests protobuf grpcio-tools

COPY docker/build_tools.py /usr/local/bin/build_tools.py
RUN chmod +x /usr/local/bin/build_tools.py

WORKDIR /workspaces/squeezelite-esp32
