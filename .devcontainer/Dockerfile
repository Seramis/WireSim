# Fedora base to match this repo's documented host packages 1:1
# (sdl2-compat-devel, cc65, verilator paths in .vscode/c_cpp_properties.json).
# fedora-minimal drops dnf for microdnf (same repos, smaller base layer).
FROM fedora-minimal:44

RUN microdnf install -y \
        clang clang-tools-extra lld \
        cmake ninja-build make \
        verilator \
        cc65 \
        z80asm cpmtools \
        sdl2-compat-devel \
        mesa-libGL mesa-dri-drivers \
        libwayland-client libwayland-cursor libwayland-egl libxkbcommon libdecor \
        libX11 libXext libXrandr libXcursor libXi libXfixes libXrender \
        git gdb socat pkgconf-pkg-config sudo which findutils shadow-utils \
        tar gzip xz \
        pdftotext valgrind strace file procps-ng \
        nodejs npm \
    && microdnf clean all

# sjasmplus (Z80 assembler for the Z80Echo machine) is not packaged for Fedora:
# build the pinned release from source (CMake, no dependencies) into /usr/local.
ARG SJASMPLUS_VERSION=1.24.0
ARG SJASMPLUS_SHA256=0b5013f07e8d8505f9e296529655668b6fdf0268625c19e883b0fff484e209f1
RUN curl -fsSL -o /tmp/sjasmplus.tar.xz \
        https://github.com/z00m128/sjasmplus/releases/download/v${SJASMPLUS_VERSION}/sjasmplus-${SJASMPLUS_VERSION}-src.tar.xz \
    && echo "${SJASMPLUS_SHA256}  /tmp/sjasmplus.tar.xz" | sha256sum -c - \
    && mkdir /tmp/sjasmplus && tar -xJf /tmp/sjasmplus.tar.xz -C /tmp/sjasmplus --strip-components=1 \
    && cmake -S /tmp/sjasmplus -B /tmp/sjasmplus/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/sjasmplus/build && cmake --install /tmp/sjasmplus/build \
    && rm -rf /tmp/sjasmplus /tmp/sjasmplus.tar.xz

# valgrind/strace/file/procps-ng back the memcheck, cache-profiling and
# syscall-tracing skills agents reach for on this performance-sensitive
# simulator (ps/kill also recover a hung WireSim run per AGENTS.md).
# perf is deliberately left out: rootless Podman's default
# perf_event_paranoid/capabilities block most of what it measures anyway.

# sdl2-compat dlopen()s its Wayland/X11 backends at runtime instead of
# linking them, so dnf never pulls those shared libs in as dependencies --
# they're listed explicitly above. Without them SDL2 silently falls back to
# the dummy driver and SDL_CreateWindow() "succeeds" with no visible window.

# No standalone claude install: shim runs the binary bundled with the VS Code
# Claude Code extension (see claude-vscode-shim).
COPY --chmod=755 claude-vscode-shim /usr/local/bin/claude

ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000

# UID/GID default to this host's user; override with --build-arg if the
# host user differs (devcontainer.json's --userns=keep-id remaps whatever
# the real host UID is onto this one, so they must match).
RUN groupadd --gid ${USER_GID} ${USERNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash ${USERNAME} \
    && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}

USER ${USERNAME}
