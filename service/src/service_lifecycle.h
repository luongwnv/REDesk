#pragma once

// REDesk service — OS service registration / lifecycle (ADR-001 §2, §3.5).
//
// REDesk runs its privileged engine as a headless OS service:
//   * Windows  : SCM service (runs as LOCAL SYSTEM in session 0; required for
//                Secure-Desktop input injection per §3.4).
//   * macOS    : LaunchDaemon (root, loaded at boot via launchd). NOTE §3.1/§3.4
//                TCC/SCK traps: a bare daemon will not appear in the Screen
//                Recording list on macOS 26.1+, and SCK fails at the login
//                window — capture must run from the *user* GUI context; the
//                daemon coordinates and owns transport. See dist/README.md.
//   * Linux    : systemd unit (system or per-user --user depending on the
//                portal/libei capability ladder in §3.4).
//
// ServiceLifecycle abstracts (un)installation + run-as-service hand-off. The
// default stub returns a portable implementation that fakes install/uninstall
// (logs intent, points at dist/ templates) and whose run_as_service() simply
// invokes the foreground entry point — enough to smoke-test the daemon wiring on
// any host. Real per-OS backends are sketched in service_lifecycle.cpp guarded
// behind #ifdef, and the deployable artifacts live under service/dist/.

#include <functional>
#include <memory>
#include <string>

#include "core/common/types.h"

namespace redesk::service {

// The actual daemon body. ServiceLifecycle::run_as_service() must arrange for
// this to be called inside the OS service context (after SCM hand-off / once
// launchd/systemd has us). It blocks until the service is asked to stop and
// returns a process exit code.
using ServiceMain = std::function<int()>;

// Parameters describing how the service is registered with the OS.
struct ServiceInstallSpec {
    std::string service_id = "com.redesk.service";   // launchd label / unit id
    std::string display_name = "REDesk Remote Desktop Service";
    std::string description = "Headless privileged REDesk capture/input/transport engine";
    std::string executable_path;                     // absolute path to this binary
    bool start_on_boot = true;
};

class ServiceLifecycle {
public:
    virtual ~ServiceLifecycle() = default;

    // Register the service with the OS service manager (write the unit/plist,
    // call the SCM, etc.). Typically requires elevation/root.
    virtual Status install(const ServiceInstallSpec& spec) = 0;

    // Remove the registration. Best-effort; succeeds if already absent.
    virtual Status uninstall(const ServiceInstallSpec& spec) = 0;

    // Run inside the OS service context: perform the SCM/launchd/systemd
    // hand-off, then invoke `body` as the long-running daemon. Returns the
    // process exit code. On hosts/builds without a real backend this just runs
    // `body` directly (foreground semantics) so the scaffold is exercisable.
    virtual int run_as_service(const ServiceInstallSpec& spec,
                               ServiceMain body) = 0;

    // Human-readable name of the active backend (for logs).
    virtual std::string backend_name() const = 0;
};

// Factory. Selects the host backend at build time; default build returns the
// portable stub.
std::unique_ptr<ServiceLifecycle> CreateServiceLifecycle();

}  // namespace redesk::service
