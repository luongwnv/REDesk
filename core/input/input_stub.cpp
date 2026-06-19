// In-memory input/clipboard stub (ADR-001 §3.4) — compiled by default
// (REDESK_USE_REAL_BACKENDS=OFF). The injector touches no OS APIs; it records
// every event into a log tests can assert against. The clipboard is a plain
// in-memory map that fires the change callback on set().
//
// TODO(ADR §3.4): real backends live under platform/<os>, selected at RUNTIME by
// capability probe (never compile-time):
//   * Windows: SendInput (virtual-desktop normalization) + Win32 clipboard;
//     Secure Desktop reach requires a SYSTEM session-0 service (OpenInputDesktop).
//   * macOS: CGEvent* + CGEventKeyboardSetUnicodeString; explicit modifier
//     down/up around chords; gate on CGPreflightPostEventAccess; NSPasteboard.
//   * Linux: portal RemoteDesktop + libei (ConnectToEIS) -> wlroots virtual-*
//     -> XTEST; upload ONE xkb keymap at session start; wl_data_control clipboard.

#include "core/input/input.h"

#include "core/common/logging.h"

namespace redesk::input {
namespace {

class StubInputInjector final : public IStubInputInjector {
public:
    Status mouseMoveAbsolute(const std::string& display_id, double nx,
                             double ny) override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::MouseMoveAbs;
        e.display_id = display_id;
        e.nx = nx;
        e.ny = ny;
        events_.push_back(std::move(e));
        return Status::success();
    }

    Status mouseMoveRelative(int32_t dx, int32_t dy) override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::MouseMoveRel;
        e.dx = dx;
        e.dy = dy;
        events_.push_back(std::move(e));
        return Status::success();
    }

    Status mouseButton(MouseButton button, ButtonAction action) override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::MouseButton;
        e.button = button;
        e.action = action;
        events_.push_back(std::move(e));
        return Status::success();
    }

    Status scroll(double dx, double dy, bool hi_res) override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::Scroll;
        e.sdx = dx;
        e.sdy = dy;
        e.hi_res = hi_res;
        events_.push_back(std::move(e));
        return Status::success();
    }

    Status keyScancode(uint32_t scancode, ButtonAction action) override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::KeyScancode;
        e.code = scancode;
        e.action = action;
        events_.push_back(std::move(e));
        return Status::success();
    }

    Status keyUnicode(uint32_t codepoint, ButtonAction action) override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::KeyUnicode;
        e.code = codepoint;
        e.action = action;
        events_.push_back(std::move(e));
        return Status::success();
    }

    Status setModifiers(Modifier modifiers) override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::SetModifiers;
        e.modifiers = modifiers;
        held_modifiers_ = modifiers;
        events_.push_back(std::move(e));
        return Status::success();
    }

    Status reset() override {
        RecordedEvent e;
        e.type = RecordedEvent::Type::Reset;
        held_modifiers_ = Modifier::None;
        events_.push_back(std::move(e));
        return Status::success();
    }

    const std::vector<RecordedEvent>& events() const override { return events_; }
    void clear() override { events_.clear(); }

private:
    std::vector<RecordedEvent> events_;
    Modifier held_modifiers_ = Modifier::None;
};

class StubClipboardSync final : public IClipboardSync {
public:
    Result<ClipboardData> get(ClipboardKind kind) override {
        for (const auto& d : store_) {
            if (d.kind == kind) {
                return Result<ClipboardData>::good(d);
            }
        }
        return Result<ClipboardData>::fail(ErrorCode::NotFound,
                                           "clipboard empty for kind");
    }

    Status set(const ClipboardData& data) override {
        // Replace any existing entry of the same kind.
        for (auto& d : store_) {
            if (d.kind == data.kind) {
                d = data;
                fire(data);
                return Status::success();
            }
        }
        store_.push_back(data);
        fire(data);
        return Status::success();
    }

    void setChangeCallback(ClipboardChangeCallback cb) override {
        change_cb_ = std::move(cb);
    }

private:
    void fire(const ClipboardData& data) {
        if (change_cb_) {
            change_cb_(data);
        }
    }

    std::vector<ClipboardData> store_;
    ClipboardChangeCallback change_cb_;
};

} // namespace

std::unique_ptr<IStubInputInjector> createStubInputInjector() {
    return std::make_unique<StubInputInjector>();
}

std::unique_ptr<IInputInjector> createInputInjector() {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.4): runtime-probe and return the platform/<os> injector.
    return createStubInputInjector();
#else
    return createStubInputInjector();
#endif
}

std::unique_ptr<IClipboardSync> createClipboardSync() {
#if defined(REDESK_USE_REAL_BACKENDS)
    // TODO(ADR §3.4): return the platform/<os> native clipboard backend.
    return std::make_unique<StubClipboardSync>();
#else
    return std::make_unique<StubClipboardSync>();
#endif
}

} // namespace redesk::input
