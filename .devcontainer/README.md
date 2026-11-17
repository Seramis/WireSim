# Dev container

Fedora 44-based container. Full build toolchain (clang, cmake, ninja, verilator, cc65, sjasmplus, z80asm, cpmtools) plus everything SDL2 device windows need to show on host.

## GUI passthrough

Container no get own display; container use host display instead. Host Wayland socket (`$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`) and XWayland X11 socket (`/tmp/.X11-unix`) bind-mounted in, same paths. `SDL_VIDEODRIVER=wayland,x11` tell SDL2 try native Wayland first, fall back X11 if fail -- work on both Wayland and X11 Linux hosts (test on Fedora KDE/Wayland).

This only work because three `runArgs` in `devcontainer.json`:

- `--userns=keep-id:uid=1000,gid=1000` -- rootless Podman map container UIDs into unprivileged host range by default. Host Wayland socket `0700` file, owned by host UID, so without this container user cannot open it at all.
- `--group-add=keep-groups` + `--device=/dev/dri` -- mean carry host `video`/`render` group membership in by GID, so SDL2 renderer get real GPU acceleration instead fall back to software (llvmpipe) rasterizer. In practice `--userns=keep-id` narrow id-range mapping squash extra host GIDs to overflow GID (confirm with `id` inside container), so this only work if `/dev/dri/renderD128` world-writable, like on tested host. If GPU accel silently fall back to llvmpipe on other machine, check that device permissions first.
- `--security-opt=label=disable` -- skip SELinux relabel of bind-mounted `$XDG_RUNTIME_DIR`. Podman usual `:Z`/`:z` relabel would rewrite SELinux context of that directory in place, break every other app on host still use it.

## GitHub Copilot

The dev container installs the GitHub Copilot and Copilot Chat VS Code extensions. Sign in to GitHub through VS Code; credentials are not mounted into the container. Project instructions, skills and custom agents are kept in the repository.

The host's `~/.copilot/copilot-instructions.md` and `~/.copilot/instructions/` are mounted read-only into the container for Copilot's user-level instructions. If missing, the host initialization command creates the file and directory before starting the container. Other Copilot state, including credentials and session logs, stays separate. Rebuild the container after changing this configuration, then start a new Copilot session. If the host's general instruction file is replaced rather than edited in place, rebuild the container to refresh its file mount.

## Podman

`.vscode/settings.json` set `dev.containers.dockerPath` to `podman`, so Dev Containers extension use it automatic -- no global VS Code settings change need.

## If `/dev/dri` doesn't exist on the host

Drop `--device=/dev/dri` from `runArgs`, set `LIBGL_ALWAYS_SOFTWARE=1` in `containerEnv`; SDL2 render through llvmpipe instead.

## GitHub credentials

Host have no git credential helper (and no `gh`); GitHub login live in VS Code own account sign-in. `.vscode/settings.json` set `dev.containers.gitCredentialHelperConfigLocation` to `none`, so Dev Containers extension stop install its helper (which forward to host `git credential fill`, find nothing, prompt for password). Git then fall through to VS Code `GIT_ASKPASS`, which answer with signed-in GitHub account token (`github.gitAuthentication`) -- for Source Control UI and for `git` run in VS Code integrated terminal (`git.terminalAuthentication`). `git` run outside VS Code terminal (e.g. `podman exec`) have no askpass, will not authenticate over HTTPS.