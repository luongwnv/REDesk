include_guard(GLOBAL)

# Applies ASan/UBSan flags globally when enabled. Clang/GCC only; no-op on MSVC
# for UBSan (MSVC ASan is opt-in via /fsanitize=address if ever needed).
add_library(redesk_sanitizers INTERFACE)

if(REDESK_ENABLE_ASAN)
    if(NOT MSVC)
        target_compile_options(redesk_sanitizers INTERFACE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(redesk_sanitizers INTERFACE -fsanitize=address)
    else()
        target_compile_options(redesk_sanitizers INTERFACE /fsanitize=address)
    endif()
endif()

if(REDESK_ENABLE_UBSAN AND NOT MSVC)
    target_compile_options(redesk_sanitizers INTERFACE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(redesk_sanitizers INTERFACE -fsanitize=undefined)
endif()
