// REDesk service — OS service lifecycle backends (ADR-001 §2, §3.5).
//
// Default stub: a portable lifecycle that logs install/uninstall intent (the
// real artifacts live in service/dist/) and runs the daemon body in the
// foreground. The three real backends are sketched below behind #ifdef and
// described so the integrator knows exactly where the OS glue goes. None of the
// real glue is compiled in the stub build.

#include "service/src/service_lifecycle.h"

#include <iostream>

namespace redesk::service {
namespace {

// Portable, dependency-free lifecycle. install()/uninstall() document what the
// real backend does and point at the dist/ templates; run_as_service() runs the
// daemon body directly (i.e. foreground semantics) so the wiring is testable on
// any host including macOS/clang stub builds.
class StubServiceLifecycle final : public ServiceLifecycle {
public:
    Status install(const ServiceInstallSpec& spec) override {
        std::cerr << "[lifecycle] (stub) would install service '"
                  << spec.service_id << "' -> " << spec.executable_path << "\n"
                  << "[lifecycle] (stub) real install copies the matching "
                     "template from service/dist/ and registers it:\n"
                  << "[lifecycle]   windows : sc.exe create / CreateService (SCM)\n"
                  << "[lifecycle]   macos   : /Library/LaunchDaemons/"
                  << spec.service_id << ".plist + launchctl bootstrap\n"
                  << "[lifecycle]   linux   : /etc/systemd/system/redesk-service.service "
                     "+ systemctl enable --now\n";
        return Status::success();
    }

    Status uninstall(const ServiceInstallSpec& spec) override {
        std::cerr << "[lifecycle] (stub) would uninstall service '"
                  << spec.service_id << "'\n";
        return Status::success();
    }

    int run_as_service(const ServiceInstallSpec& spec,
                       ServiceMain body) override {
        std::cerr << "[lifecycle] (stub) running daemon body in foreground "
                     "(no SCM/launchd/systemd hand-off)\n";
        (void)spec;
        return body ? body() : 0;
    }

    std::string backend_name() const override { return "stub-foreground"; }
};

// ===========================================================================
// REAL BACKENDS — sketched, not compiled in the stub build. Each describes the
// exact OS integration the integrator must flesh out. They intentionally fall
// through to the foreground body for now so the tree links if a backend macro is
// flipped on prematurely.
// ===========================================================================

#if defined(REDESK_REAL_LIFECYCLE) && defined(_WIN32)
// Windows SCM service backend.
//
//   install():    OpenSCManager + CreateService(SERVICE_WIN32_OWN_PROCESS,
//                 SERVICE_AUTO_START, lpServiceStartName = "LocalSystem"). Set a
//                 SERVICE_DESCRIPTION + failure actions (SetServiceObjectSecurity
//                 to restrict who can control it).
//   uninstall():  OpenService + ControlService(STOP) + DeleteService.
//   run_as_service(): StartServiceCtrlDispatcher with a ServiceMain that calls
//                 RegisterServiceCtrlHandlerEx, reports SERVICE_RUNNING, then
//                 runs `body`. CRITICAL (§3.4): to inject into the Secure Desktop
//                 the service must run as SYSTEM in session 0 and hop desktops on
//                 WTS_SESSION_CHANGE / desktop-switch events.
class WinScmLifecycle final : public ServiceLifecycle { /* ... */ };
#endif

#if defined(REDESK_REAL_LIFECYCLE) && defined(__APPLE__)
// macOS LaunchDaemon backend.
//
//   install():    write service/dist/com.redesk.service.plist to
//                 /Library/LaunchDaemons/, chown root:wheel, chmod 644, then
//                 `launchctl bootstrap system <plist>` (modern) or load -w.
//   uninstall():  `launchctl bootout system/<label>` + remove the plist.
//   run_as_service(): launchd KeepAlive-managed; just run `body`. NOTE (§3.1):
//                 the daemon CANNOT do ScreenCaptureKit capture itself (no GUI
//                 context, not in the Screen Recording list on 26.1+). Capture +
//                 CGEvent injection run in a per-user GUI agent (SMAppService /
//                 LaunchAgent) approved via MDM PPPC; this daemon owns transport
//                 + coordination and brokers to that agent over IPC.
class MacLaunchDaemonLifecycle final : public ServiceLifecycle { /* ... */ };
#endif

#if defined(REDESK_REAL_LIFECYCLE) && defined(__linux__)
// Linux systemd backend.
//
//   install():    write service/dist/redesk-service.service to
//                 /etc/systemd/system/, `systemctl daemon-reload`, then
//                 `systemctl enable --now redesk-service`.
//   uninstall():  `systemctl disable --now redesk-service` + remove unit.
//   run_as_service(): under Type=notify, call sd_notify(0,"READY=1") once up,
//                 sd_notify watchdog pings if WatchdogSec is set, then run
//                 `body`. Honor SIGTERM for graceful stop. Wayland portal/libei
//                 (§3.4) may require a --user instance bound to the graphical
//                 session instead of/in addition to this system unit.
class SystemdLifecycle final : public ServiceLifecycle { /* ... */ };
#endif

}  // namespace

std::unique_ptr<ServiceLifecycle> CreateServiceLifecycle() {
    // TODO(ADR §2): when REDESK_REAL_LIFECYCLE is defined, return the host
    // backend (WinScmLifecycle / MacLaunchDaemonLifecycle / SystemdLifecycle).
    // The default portable stub keeps the scaffold runnable everywhere.
    return std::make_unique<StubServiceLifecycle>();
}

}  // namespace redesk::service
