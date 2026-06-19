# REDesk — Qt Quick UI client (ADR-001 §3.5)
#
# This is the unprivileged control-panel + remote-video client. It speaks to the
# privileged service/daemon over an authenticated QLocalSocket (named pipe / UDS)
# and presents decoded frames zero-copy via QQuickRhiItem (ADR §3.5).
#
# GRACEFUL SELF-DISABLE: the verification / portable stub build has no Qt. If Qt6
# is not found we emit a STATUS line and return() so the *rest* of the tree still
# configures and builds. Nothing below the guard runs without Qt.

# GuiPrivate/QuickPrivate are needed for QQuickRhiItem's zero-copy present path
# (ADR §3.5). They are separate find_package COMPONENTS, not just link targets.
find_package(Qt6 6.8 COMPONENTS Quick Gui Core Network GuiPrivate QuickPrivate QUIET)

if(NOT Qt6_FOUND)
    message(STATUS "REDesk UI: Qt6 (Quick/Gui/Core/Network) not found — UI target disabled.")
    message(STATUS "           Install Qt 6.8+ and reconfigure to build redesk_ui.")
    return()
endif()

message(STATUS "REDesk UI: Qt6 ${Qt6_VERSION} found — building redesk_ui.")

# QML/Qt require the meta-object compiler and automatic resource handling.
qt_standard_project_setup(REQUIRES 6.8)

# ---------------------------------------------------------------------------
# Executable + C++ sources
# ---------------------------------------------------------------------------
qt_add_executable(redesk_ui
    src/main.cpp
    src/VideoItem.cpp
    src/VideoItem.h
    src/ServiceClient.cpp
    src/ServiceClient.h
    src/SessionController.cpp
    src/SessionController.h
    # Phase-1 demo: real-screen preview via the CPU/QImage path.
    src/LocalScreenSource.cpp
    src/LocalScreenSource.h
    src/ScreenView.cpp
    src/ScreenView.h
)

# ---------------------------------------------------------------------------
# QML module — Main.qml is the application entry view. Components are grouped in
# their own subfolder and exposed under the same import URI.
# ---------------------------------------------------------------------------
# Theme.qml declares `pragma Singleton`; the QML module must be told so the
# singleton type is registered (otherwise the compiled module fails to resolve
# `Theme` when deployed). Required before qt_add_qml_module.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(redesk_ui
    URI REDesk.UI
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/SessionView.qml
        qml/LocalScreenPreview.qml
        qml/Theme.qml
        qml/components/Card.qml
        qml/components/PrimaryButton.qml
        qml/components/IdField.qml
        qml/components/Pill.qml
)

# QQuickRhiItem lives in the Quick module but the zero-copy present path needs
# Qt's private GUI/Quick RHI headers (ADR §3.5: "QRhi is QPA-tier, requires
# Qt::GuiPrivate"). These are version-pinned and isolated behind VideoItem.
target_link_libraries(redesk_ui
    PRIVATE
        Qt6::Quick
        Qt6::Gui
        Qt6::Core
        Qt6::Network
        Qt6::GuiPrivate
        Qt6::QuickPrivate
)

# Link the platform-abstracted engine for shared value types (redesk::VideoFrame,
# Status, Result, the IPC protocol enums, ...). The core slice exposes an ALIAS
# target REDesk::core; guard the link so the UI still builds if core is sliced in
# separately or temporarily disabled.
if(TARGET REDesk::core)
    target_link_libraries(redesk_ui PRIVATE REDesk::core)
elseif(TARGET redesk_core)
    target_link_libraries(redesk_ui PRIVATE redesk_core)
endif()

# Link the native platform backend so the UI can prefer CreateNativeCapturer()
# (ScreenCaptureKit/DXGI/PipeWire) and fall back to the core stub. Inherits the
# REDESK_USE_REAL_BACKENDS PUBLIC compile definition from this target.
if(TARGET REDesk::platform)
    target_link_libraries(redesk_ui PRIVATE REDesk::platform)
elseif(TARGET redesk_platform)
    target_link_libraries(redesk_ui PRIVATE redesk_platform)
endif()

# Repo-root-relative includes ("core/common/types.h") and the generated version
# header (generated/redesk_version.h from the top-level configure_file).
target_include_directories(redesk_ui
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}"            # allows "generated/redesk_version.h"
        "${CMAKE_BINARY_DIR}/generated"  # allows "redesk_version.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src" # qmltyperegistrations uses <Header.h>
)

target_compile_features(redesk_ui PRIVATE cxx_std_20)

set_target_properties(redesk_ui PROPERTIES
    WIN32_EXECUTABLE ON      # no console window on Windows GUI build
    MACOSX_BUNDLE ON         # ADR §3.1: notarized .app bundle required for TCC
    OUTPUT_NAME "REDesk"
)

if(APPLE)
    set_target_properties(redesk_ui PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "REDesk"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.redesk.ui"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        # Info.plist carries NSScreenCaptureUsageDescription (TCC prompt, ADR §3.1).
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
    )
endif()
