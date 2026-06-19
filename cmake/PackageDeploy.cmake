# REDesk packaging & deployment (ADR-001 §5).
#
# Bundles the Qt runtime into the app and produces an installer/disk image per OS:
#   * macOS : macdeployqt -> self-contained REDesk.app -> .dmg (CPack DragNDrop)
#   * Windows: windeployqt -> runtime next to REDesk.exe -> NSIS installer (CPack)
#   * Linux : a portable layout -> .AppImage (linuxdeploy) and/or .deb (CPack DEB)
#
# This module only WIRES the steps; building an actual Windows/Linux installer
# requires running on that OS (or CI), since windeployqt/linuxdeploy are native.
# On macOS the .dmg builds locally: `cmake --build build-mac --target package`.
#
# Guarded so it is a no-op unless the UI (the only bundled GUI artifact) is built.

include_guard(GLOBAL)

if(NOT TARGET redesk_ui)
    return()
endif()

# Locate Qt's deploy tool (ships in the Qt bin dir alongside qmake).
find_program(QT_DEPLOY_QMLIMPORTSCANNER NAMES qmlimportscanner
    HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")

# ---------------------------------------------------------------------------
# Per-OS runtime deployment (run at install time so CPack picks up the result).
# ---------------------------------------------------------------------------
if(APPLE)
    find_program(MACDEPLOYQT macdeployqt
        HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    # Install the bundle FIRST, then run macdeployqt on it (install() order is
    # significant: the TARGETS rule must place REDesk.app before the CODE rule
    # embeds Qt into it).
    install(TARGETS redesk_ui BUNDLE DESTINATION . COMPONENT gui)
    if(MACDEPLOYQT)
        # macdeployqt rewrites the binary's rpaths to point at the embedded Qt,
        # which INVALIDATES the linker's signature -> macOS kills the app at dyld
        # with "Code Signature Invalid". We must re-sign AFTER deploying. An
        # ad-hoc signature (-s -) is enough to launch locally; a Developer ID +
        # notarization is required for distribution to other Macs (ADR §3.1).
        install(CODE "
            set(_app \"\${CMAKE_INSTALL_PREFIX}/REDesk.app\")
            message(STATUS \"macdeployqt: bundling Qt into REDesk.app\")
            execute_process(COMMAND \"${MACDEPLOYQT}\" \"\${_app}\"
                -qmldir=${CMAKE_SOURCE_DIR}/ui/qml)
            # macdeployqt rewrites rpaths (breaking the linker signature) and
            # copies Homebrew-signed Qt frameworks whose Team IDs do NOT match our
            # binary -> dyld rejects them. Re-sign INSIDE-OUT with a single ad-hoc
            # identity so every nested framework/dylib + the outer bundle share
            # it. (--deep is deprecated and unreliable here, so we walk manually.)
            message(STATUS \"codesign: ad-hoc re-signing nested code, then bundle\")
            file(GLOB_RECURSE _dylibs \"\${_app}/Contents/Frameworks/*.dylib\")
            file(GLOB _fw \"\${_app}/Contents/Frameworks/*.framework\")
            file(GLOB_RECURSE _plugins \"\${_app}/Contents/PlugIns/*.dylib\")
            foreach(_item \${_dylibs} \${_fw} \${_plugins})
                execute_process(COMMAND codesign --force --sign - \"\${_item}\"
                                ERROR_QUIET)
            endforeach()
            execute_process(COMMAND codesign --force --sign - \"\${_app}\"
                            RESULT_VARIABLE _cs)
            if(NOT _cs EQUAL 0)
                message(WARNING \"codesign failed (rc=\${_cs}); the app may not launch.\")
            endif()
        " COMPONENT gui)
    endif()
    # The .dmg ships only the GUI app (not the server/test install artifacts).
    set(CPACK_COMPONENTS_ALL gui)

elseif(WIN32)
    find_program(WINDEPLOYQT windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    install(TARGETS redesk_ui RUNTIME DESTINATION .)
    if(WINDEPLOYQT)
        install(CODE "
            message(STATUS \"windeployqt: staging Qt runtime next to REDesk.exe\")
            execute_process(COMMAND \"${WINDEPLOYQT}\"
                --qmldir \"${CMAKE_SOURCE_DIR}/ui/qml\" --release --no-translations
                \"\${CMAKE_INSTALL_PREFIX}/REDesk.exe\")
        ")
    endif()

else() # Linux
    install(TARGETS redesk_ui RUNTIME DESTINATION bin)
    # AppImage/.deb runtime bundling is done by linuxdeploy + the linuxdeploy-qt
    # plugin in CI (it needs the assembled install tree); see packaging/linux/.
    install(CODE "message(STATUS \"Linux: run linuxdeploy --plugin qt on the install tree to produce an AppImage\")")
endif()

# ---------------------------------------------------------------------------
# CPack — installer generators per OS.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "REDesk")
set(CPACK_PACKAGE_VENDOR "REDesk")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "REDesk")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")          # -> REDesk-<ver>.dmg
    set(CPACK_DMG_VOLUME_NAME "REDesk")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS")               # -> REDesk-<ver>.exe installer
    set(CPACK_NSIS_PACKAGE_NAME "REDesk")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "REDesk.exe")
else()
    set(CPACK_GENERATOR "DEB;TGZ")            # -> .deb + portable tarball
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "REDesk")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # Runtime deps so `apt install ./REDesk.deb` pulls Qt + its QML plugins
    # (the app links Qt dynamically; the QML modules are loaded at runtime and
    # are NOT caught by shlibdeps). Names match Ubuntu 24.04.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6quick6, libqt6gui6, libqt6qml6, libqt6network6, \
qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-templates, \
qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtqml-workerscript")
endif()

include(CPack)
