# JobPilotAI service image (0016 P1).
#
# Two stages, so the runtime layer carries the binary and its shared libraries
# only: the builder installs the toolchain, resolves the pinned Conan graph
# through the repository's own bootstrap script (the same script a local build
# runs, so the two paths cannot drift) and compiles the service target.
#
# Build:  docker build -t jobpilot-backend:local .
# Run:    docker compose --profile app up -d       (see docker-compose.yml)

# ---------------------------------------------------------------- builder ---
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        libmariadb-dev \
        libmariadb-dev-compat \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

# Conan 2 lives in its own virtualenv: Ubuntu 24.04 marks the system Python as
# externally managed, and the venv keeps the toolchain out of the runtime image.
RUN python3 -m venv /opt/conan \
    && /opt/conan/bin/pip install --no-cache-dir "conan>=2,<3"

WORKDIR /src

# The dependency layer depends only on the lock, the conanfile and the bootstrap
# script, so editing application sources reuses it instead of re-resolving the
# graph. `COPY . .` below cannot clobber service/build: .dockerignore excludes it.
COPY service/conanfile.txt service/conan.lock ./service/
COPY tools/dev/bootstrap.sh ./tools/dev/bootstrap.sh
RUN CONAN=/opt/conan/bin/conan ./tools/dev/bootstrap.sh

COPY . .

# Only the service target: the test targets are built and run by CI and by the
# stage's L4, not inside the image.
RUN cmake --preset release \
    && cmake --build --preset release --target jobpilot_service -j"$(nproc)"

# ---------------------------------------------------------------- runtime ---
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# libmariadb3 is the only non-system library the binary links (the Conan
# dependencies are linked statically); apt pulls its own dependencies. The
# MariaDB Connector/C client library is LGPL-2.1-or-later and is linked
# dynamically; see THIRD_PARTY_NOTICES.md for the factual provenance note.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        libmariadb3 \
    && rm -rf /var/lib/apt/lists/*

# The documented run command starts the service from the repository root: the
# platform-name registry default and the relative storage root both depend on
# that working directory.
WORKDIR /app
COPY --from=builder /src/build/presets/release/service/jobpilot_service \
     ./build/presets/release/service/jobpilot_service
COPY --from=builder /src/service/config/config.yaml ./service/config/config.yaml
COPY --from=builder /src/source_adapters/jobpilot_sources/data/platform_names.yaml \
     ./source_adapters/jobpilot_sources/data/platform_names.yaml

# STORAGE_ROOT defaults to `storage`, relative to this directory, and the
# compose file mounts the resume_storage volume here so uploaded resumes
# outlive the container.
RUN mkdir -p /app/storage

EXPOSE 8080

CMD ["./build/presets/release/service/jobpilot_service", "--config=service/config/config.yaml"]
