ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# Python drives the smoke test; application dependencies must come from the release.
# No developer packages, compiler, audio server or additional application libraries.
RUN if command -v apt-get >/dev/null; then \
      apt-get update && \
      apt-get install -y --no-install-recommends python3 ca-certificates && \
      rm -rf /var/lib/apt/lists/*; \
    elif command -v dnf >/dev/null; then \
      dnf install -y --setopt=install_weak_deps=False python3 ca-certificates && \
      dnf clean all; \
    elif command -v pacman >/dev/null; then \
      pacman -Syu --noconfirm --needed python ca-certificates && \
      pacman -Scc --noconfirm; \
    else exit 1; fi

ENV LANG=C.UTF-8 TERM=xterm-256color
WORKDIR /tmp
