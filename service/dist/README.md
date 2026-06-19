# REDesk Service — Install Templates

This directory holds the OS service-registration templates for the headless
privileged **`redesk-service`** daemon (ADR-001 §2, §3.5). The daemon owns
capture, encode, input injection, and the network transport, and exposes an
authenticated, ACL-hardened local IPC channel to the unprivileged Qt UI client.

The daemon supports four modes:

| Mode | Purpose |
|------|---------|
| `redesk-service --install`    | Register with the OS service manager |
| `redesk-service --uninstall`  | Remove the registration |
| `redesk-service --run`        | Run as a real OS service (SCM/launchd/systemd hand-off) |
| `redesk-service --foreground` | Run inline (dev/smoke pipeline) |

> In the default stub build (`REDESK_USE_REAL_BACKENDS=OFF`) `--foreground` runs a
> real capture → encode → transport smoke pipeline with no external deps, logging
> each step, then exits after a few seconds. Install/run modes log their intent
> via the portable stub lifecycle and point here.

---

## Windows — SCM service

There is no checked-in file for Windows; the service registers itself with the
Service Control Manager programmatically.

```bat
:: Register (run from an elevated prompt)
redesk-service.exe --install
:: Or manually:
sc.exe create REDeskService binPath= "\"C:\Program Files\REDesk\redesk-service.exe\" --run" start= auto obj= LocalSystem
sc.exe description REDeskService "REDesk Remote Desktop Service"
sc.exe start REDeskService

:: Remove
redesk-service.exe --uninstall
sc.exe delete REDeskService
```

Requirements (ADR §3.4):

- Runs as **LOCAL SYSTEM in session 0** — required to reach the **Secure Desktop**
  (UAC/logon/lock) by hopping desktops on `WTS_SESSION_CHANGE` / desktop-switch
  events. `uiAccess` alone does **not** reach the Secure Desktop.
- Install into a **trusted path** (`C:\Program Files\REDesk\...` or `System32`)
  and **Authenticode-sign** the binary, or input injection into higher-integrity
  windows will be blocked.
- The IPC named pipe (`\\.\pipe\REDesk-service`) must carry an explicit
  **security descriptor** granting only the intended user SID(s) + Administrators
  + SYSTEM and **denying Everyone/anonymous** (ADR §3.5).

---

## macOS — LaunchDaemon

Template: [`com.redesk.service.plist`](./com.redesk.service.plist)

```sh
sudo cp com.redesk.service.plist /Library/LaunchDaemons/
sudo chown root:wheel /Library/LaunchDaemons/com.redesk.service.plist
sudo chmod 644        /Library/LaunchDaemons/com.redesk.service.plist
sudo launchctl bootstrap system /Library/LaunchDaemons/com.redesk.service.plist
sudo launchctl enable system/com.redesk.service

# Remove
sudo launchctl bootout system/com.redesk.service
sudo rm /Library/LaunchDaemons/com.redesk.service.plist
```

**TCC traps (ADR §3.1 / §3.4) — read before shipping:**

- A LaunchDaemon has **no GUI/window-server context**: ScreenCaptureKit capture
  and CGEvent injection **cannot** run in it and won't appear in the Screen
  Recording / Accessibility lists on macOS 26.1+.
- Run capture + injection from a **per-user, signed + notarized `.app` /
  LaunchAgent** (registered via `SMAppService`), pre-approved through
  **MDM-delivered PPPC/TCC profiles**. This daemon owns **transport +
  coordination** and brokers to the user agent over the local IPC channel.
- Screen Recording must be **re-confirmed roughly monthly and after reboot**;
  budget re-grant onboarding UX.
- **SCK fails at the Login Window** from a daemon — treat login-screen control as
  a scoped research item, not a guarantee.
- Distribute **Developer ID** (not Mac App Store — the sandbox breaks PID event
  taps for injection).

---

## Linux — systemd unit

Template: [`redesk-service.service`](./redesk-service.service)

```sh
# System unit (transport/coordination engine as root)
sudo cp redesk-service.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now redesk-service

# Remove
sudo systemctl disable --now redesk-service
sudo rm /etc/systemd/system/redesk-service.service
sudo systemctl daemon-reload
```

**Wayland capability ladder (ADR §3.4):**

- Portal/libei input + ScreenCast capture are bound to a **graphical user
  session**. A root system unit can own transport/coordination, but capture +
  injection on Wayland usually need a **per-user instance** bound to the user's
  graphical session:

  ```sh
  mkdir -p ~/.config/systemd/user
  cp redesk-service.service ~/.config/systemd/user/
  # edit ExecStart/User as needed for the --user context
  systemctl --user enable --now redesk-service
  ```

- Probe `ConnectToEIS` (libei) availability at runtime — **never** infer from
  compositor name/version. Floor is **GNOME ≥46 / KDE Plasma ≥6.1**. Degrade to
  view-only with a clear message where injection is unsupported.
- The `restore_token` for the ScreenCast/RemoteDesktop portal is single-use and
  rotates each `Start` — write it **atomically (temp + rename) immediately** after
  each successful Start.

---

## IPC socket location & hardening (all OSes)

- The local IPC endpoint must **not** live in a per-session object namespace
  (a low-priv session could squat the name).
- The endpoint must be **owner/ACL-restricted** (UDS `0600` or a locked-down
  group; Windows security descriptor) and its parent directory root-owned `0755`
  (`/run/redesk` via `RuntimeDirectory=redesk`) so the path can't be hijacked.
- Every connection is **peer-authenticated** (`SO_PEERCRED`/`LOCAL_PEERCRED` /
  `GetNamedPipeClientProcessId`) and **every message is validated as untrusted**
  before it reaches the engine (ADR §3.5).
