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

    # Phase 1: only the capturer is wired to the real core interface. The
    # encoder/input/keystore .mm files still carry the original cross-slice
    # signature *guesses* (e.g. core/codec/encoder.h, PascalCase pull API) and
    # are not yet reconciled with the real core headers, so they are excluded
    # from the build for now. The always-compiled stub_macos.cpp supplies their
    # null CreateNative*() factories. Re-add each .mm as it is reconciled.
    # TODO(Phase 1+): reconcile encoder_videotoolbox.mm / input_cgevent.mm /
    #                 keystore_keychain.mm against core and re-enable here.
    target_sources(redesk_platform PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/capturer_sck.mm"        # ScreenCaptureKit (wired)
        "${CMAKE_CURRENT_SOURCE_DIR}/unwired_macos.cpp"      # null encoder/input/keystore
    )

    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/capturer_sck.mm"
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
