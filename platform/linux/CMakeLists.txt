# platform/linux/ — Linux native backends (ADR-001 §3.1/§3.2/§3.4/§3.6).
#
# Sources are appended to the aggregate `redesk_platform` target. Every real
# backend body is guarded by `#if REDESK_USE_REAL_BACKENDS`; in the default
# build these TUs compile to thin stub shims with NO PipeWire/portal/VAAPI/
# libei/libsecret dependency.
#
# The INPUT backend is selected at RUNTIME by a capability probe (ADR §3.4):
# portal+libei -> wlroots virtual-pointer/keyboard -> XTEST. CMake only links
# the libraries; the ladder choice is never compile-time.

target_sources(redesk_platform PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/capturer_pipewire.cpp"  # PipeWire-portal + XShm
    "${CMAKE_CURRENT_SOURCE_DIR}/encoder_vaapi.cpp"      # VAAPI / NVENC
    "${CMAKE_CURRENT_SOURCE_DIR}/input_ladder.cpp"       # runtime capability ladder
    "${CMAKE_CURRENT_SOURCE_DIR}/keystore_libsecret.cpp" # libsecret
)

if(REDESK_USE_REAL_BACKENDS)
    find_package(PkgConfig REQUIRED)

    # Discover deps via pkg-config; keep them PRIVATE to redesk_platform.
    # (ADR §4 license flags: libpipewire MIT, libva MIT, libei MIT,
    #  libsecret LGPL, X11/Xext/Xfixes/Xtst MIT/X11.)
    pkg_check_modules(PIPEWIRE   REQUIRED IMPORTED_TARGET libpipewire-0.3)
    pkg_check_modules(LIBVA      REQUIRED IMPORTED_TARGET libva libva-drm)
    pkg_check_modules(LIBEI      IMPORTED_TARGET          libei-1.0)   # GNOME>=46/KDE>=6.1
    pkg_check_modules(LIBSECRET  REQUIRED IMPORTED_TARGET libsecret-1)
    pkg_check_modules(GIO        REQUIRED IMPORTED_TARGET gio-2.0)     # portal D-Bus
    pkg_check_modules(X11FALLBACK IMPORTED_TARGET         x11 xext xfixes xtst)

    target_link_libraries(redesk_platform PRIVATE
        PkgConfig::PIPEWIRE
        PkgConfig::LIBVA
        PkgConfig::LIBSECRET
        PkgConfig::GIO
    )
    if(LIBEI_FOUND)
        target_link_libraries(redesk_platform PRIVATE PkgConfig::LIBEI)
        target_compile_definitions(redesk_platform PRIVATE REDESK_HAVE_LIBEI=1)
    endif()
    if(X11FALLBACK_FOUND)
        target_link_libraries(redesk_platform PRIVATE PkgConfig::X11FALLBACK)
        target_compile_definitions(redesk_platform PRIVATE REDESK_HAVE_X11=1)
    endif()
    # NVENC on Linux is the proprietary Video Codec SDK (located via cmake/ SDK
    # locators), added here when present. VAAPI is the default open path.
endif()
