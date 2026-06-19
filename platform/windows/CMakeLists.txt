# platform/windows/ — Windows native backends (ADR-001 §3.1/§3.2/§3.4/§3.6).
#
# Sources are appended to the aggregate `redesk_platform` target defined in
# platform/CMakeLists.txt. Every real backend body is guarded by
# `#if REDESK_USE_REAL_BACKENDS`; in the default build these translation units
# compile to thin stub-registration shims (CreateNative*() -> nullptr) with no
# Windows-SDK / vendor-SDK dependency.

target_sources(redesk_platform PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/capturer_dxgi.cpp"      # DDA + WGC fallback
    "${CMAKE_CURRENT_SOURCE_DIR}/encoder_mf.cpp"         # NVENC/AMF/QSV/MF selector
    "${CMAKE_CURRENT_SOURCE_DIR}/input_sendinput.cpp"    # SendInput + desktop-hop
    "${CMAKE_CURRENT_SOURCE_DIR}/keystore_dpapi.cpp"     # DPAPI-CNG keystore
)

if(REDESK_USE_REAL_BACKENDS)
    # System libraries shipped with the Windows SDK. Vendor encode SDKs
    # (NVENC/AMF/oneVPL) are located via cmake/ SDK locators and added here as
    # they are vendored (ADR §3.2: call vendor SDKs directly, NOT via FFmpeg).
    target_link_libraries(redesk_platform PRIVATE
        d3d11        # D3D11 device for capture + GPU color convert
        dxgi         # DXGI Desktop Duplication
        dxguid
        mf mfplat mfuuid mfreadwrite  # Media Foundation (last-resort encoder)
        user32       # SendInput / OpenInputDesktop / SetThreadDesktop
        crypt32      # DPAPI (CryptProtectData) — fallback path
        ncrypt bcrypt# CNG / DPAPI-NG (NCryptProtectSecret) — preferred path
        wtsapi32     # WTS session-change notifications (desktop-hop)
        advapi32     # SID / security-descriptor helpers (named-pipe ACL)
        # windowsapp # WinRT (Windows.Graphics.Capture / ApiInformation) — link
                     # when the WGC path is enabled; requires C++/WinRT headers.
    )
    target_compile_definitions(redesk_platform PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    )
endif()
