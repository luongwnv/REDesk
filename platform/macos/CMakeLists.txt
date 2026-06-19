# platform/macos/ — macOS native backends (ADR-001 §3.1/§3.2/§3.4/§3.6).
#
# Sources are appended to the aggregate `redesk_platform` target.
#
# Build contract: the ObjC++ (.mm) backends are added to the build ONLY when
# (APPLE AND REDESK_USE_REAL_BACKENDS) so a DEFAULT macOS build never invokes
# the ObjC toolchain or links Apple media/Quartz frameworks. The always-compiled
# plain-C++ stub_macos.cpp provides the null CreateNative*() factories for the
# default build. (The .mm files define the real CreateNative*() and are only
# present on the real path, so there is exactly one definition per config.)

if(APPLE AND REDESK_USE_REAL_BACKENDS)
    enable_language(OBJCXX)

    target_sources(redesk_platform PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/capturer_sck.mm"        # ScreenCaptureKit
        "${CMAKE_CURRENT_SOURCE_DIR}/encoder_videotoolbox.mm" # VideoToolbox
        "${CMAKE_CURRENT_SOURCE_DIR}/input_cgevent.mm"       # CGEvent injector
        "${CMAKE_CURRENT_SOURCE_DIR}/keystore_keychain.mm"   # Keychain + SEP wrap
    )

    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/capturer_sck.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/encoder_videotoolbox.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/input_cgevent.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/keystore_keychain.mm"
        PROPERTIES
            COMPILE_FLAGS "-fobjc-arc"   # ARC for the ObjC object graph
    )

    target_link_libraries(redesk_platform PRIVATE
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework CoreGraphics"        # CGEvent injection
        "-framework CoreMedia"
        "-framework CoreVideo"           # CVPixelBuffer / IOSurface-backed frames
        "-framework IOSurface"
        "-framework ScreenCaptureKit"    # SCStream capture
        "-framework VideoToolbox"        # HW encode (RealTime)
        "-framework Security"            # Keychain + SecureEnclave (P-256 wrap)
        "-framework LocalAuthentication" # SEP access control (optional gate)
    )
else()
    # Default build (incl. default macOS): only the portable stub TU.
    target_sources(redesk_platform PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/stub_macos.cpp"
    )
endif()
