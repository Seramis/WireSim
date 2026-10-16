# Fedora base to match this repo's documented host packages 1:1
# (sdl2-compat-devel, cc65, verilator paths in .vscode/c_cpp_properties.json).
# fedora-minimal drops dnf for microdnf (same repos, smaller base layer).
FROM fedora-minimal:44

RUN microdnf install -y \
        clang clang-tools-extra lld \
        cmake ninja-build make \
        verilator \
        cc65 \
        sdl2-compat-devel \
        mesa-libGL mesa-dri-drivers \
        libwayland-client libwayland-cursor libwayland-egl libxkbcommon libdecor \
        libX11 libXext libXrandr libXcursor libXi libXfixes libXrender \
        git gdb socat pkgconf-pkg-config sudo which findutils shadow-utils \
        tar gzip \
        pdftotext valgrind strace file procps-ng \
        nodejs npm \
    && microdnf clean all

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
