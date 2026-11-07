set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_CROSSCOMPILING TRUE)

set(TCHAIN_ROOT "/nix/store/3fr3xdg8jwyzr5a5y13fzvv9g47yw926-i686-w64-mingw32-gcc-15.2.0")
set(RUNTIME_ROOT "/nix/store/2m5s2w1hc3kf56i0mryri2fnkyvy5pa5-mingw-w64-i686-w64-mingw32-13.0.0")
set(MCFG_DEV "/nix/store/l7xspmfvr51mj9g44qdj2cpq6cs403iw-mcfgthread-i686-w64-mingw32-2.3.1-dev")
set(MCFG_LIB "/nix/store/qic29l8an3j0p18s5ywnblnzsah2havf-mcfgthread-i686-w64-mingw32-2.3.1")
set(BINUTILS "/nix/store/6zigdbx74ya8b85aj4vswpbgazpkfaxv-i686-w64-mingw32-binutils-wrapper-2.44")

set(CMAKE_C_COMPILER ${TCHAIN_ROOT}/bin/i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER ${TCHAIN_ROOT}/bin/i686-w64-mingw32-g++)
set(CMAKE_RC_COMPILER ${TCHAIN_ROOT}/bin/i686-w64-mingw32-windres)
set(CMAKE_ASM_COMPILER ${TCHAIN_ROOT}/bin/i686-w64-mingw32-gcc)

set(CMAKE_SYSTEM_PREFIX_PATH ${RUNTIME_ROOT})
set(CMAKE_FIND_ROOT_PATH ${RUNTIME_ROOT} ${TCHAIN_ROOT} ${MCFG_DEV})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CXX_FLAGS_INIT "-I${MCFG_DEV}/include")
set(CMAKE_C_FLAGS_INIT "-I${MCFG_DEV}/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -B${RUNTIME_ROOT}/lib -L${MCFG_LIB}/lib -lmcfgthread")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)