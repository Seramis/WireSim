# Dev container

Fedora 44-based container. Full build toolchain (clang, cmake, ninja, verilator, cc65, sjasmplus) plus everything SDL2 device windows need to show on host.

## GUI passthrough

Container no get own display; container use host display instead. Host Wayland socket (`$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`) and XWayland X11 socket (`/tmp/.X11-unix`) bind-mounted in, same paths. `SDL_VIDEODRIVER=wayland,x11` tell SDL2 try native Wayland first, fall back X11 if fail -- work on both Wayland and X11 Linux hosts (test on Fedora KDE/Wayland).

This only work because three `runArgs` in `devcontainer.json`:

- `--userns=keep-id:uid=1000,gid=1000` -- rootless Podman map container UIDs into unprivileged host range by default. Host Wayland socket `0700` file, owned by host UID, so without this container user cannot open it at all.
- `--group-add=keep-groups` + `--device=/dev/dri` -- mean carry host `video`/`render` group membership in by GID, so SDL2 renderer get real GPU acceleration instead fall back to software (llvmpipe) rasterizer. In practice `--userns=keep-id` narrow id-range mapping squash extra host GIDs to overflow GID (confirm with `id` inside container), so this only work if `/dev/dri/renderD128` world-writable, like on tested host. If GPU accel silently fall back to llvmpipe on other machine, check that device permissions first.
- `--security-opt=label=disable` -- skip SELinux relabel of bind-mounted `$XDG_RUNTIME_DIR`. Podman usual `:Z`/`:z` relabel would rewrite SELinux context of that directory in place, break every other app on host still use it.

## Claude Code credentials

`~/.claude` and `~/.claude.json` (OAuth tokens, account/trust config, skills, memory) bind-mounted from host into `/home/dev/.claude` and `/home/dev/.claude.json` -- read-write, at runtime, never copy into image -- so Claude Code inside container already logged in, share history/memory with host instead need own login.

This mount *whole* `~/.claude` tree, include session transcripts and plans from other projects on host, not just credential files. That intentional tradeoff for full parity with host CLI/IDE experience (same auto-memory, same skills). If too broad, narrow mount in `devcontainer.json` to just two files that actually carry auth: `~/.claude/.credentials.json` and `~/.claude.json`.

`.credentials.json` hold plaintext OAuth access/refresh token pair -- treat like any other secret.

## Podman

`.vscode/settings.json` set `dev.containers.dockerPath` to `podman`, so Dev Containers extension use it automatic -- no global VS Code settings change need.

## If `/dev/dri` doesn't exist on the host

Drop `--device=/dev/dri` from `runArgs`, set `LIBGL_ALWAYS_SOFTWARE=1` in `containerEnv`; SDL2 render through llvmpipe instead.

## GitHub credentials

Host have no git credential helper (and no `gh`); GitHub login live in VS Code own account sign-in. `.vscode/settings.json` set `dev.containers.gitCredentialHelperConfigLocation` to `none`, so Dev Containers extension stop install its helper (which forward to host `git credential fill`, find nothing, prompt for password). Git then fall through to VS Code `GIT_ASKPASS`, which answer with signed-in GitHub account token (`github.gitAuthentication`) -- for Source Control UI and for `git` run in VS Code integrated terminal (`git.terminalAuthentication`). `git` run outside VS Code terminal (e.g. `podman exec`) have no askpass, will not authenticate over HTTPS.