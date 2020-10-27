# This module exposes following variables to the project:
# * HOST_C_FLAGS
# * DYNASM_ARCH
# * DYNASM_FLAGS

# XXX: buildvm includes core headers and thus has to be built
# with the same flags and defines as the LuaJIT core itself.
set(HOST_C_FLAGS)
set(DYNASM_ARCH)
set(DYNASM_FLAGS)

# XXX: <execute_process> simply splits the COMMAND argument by
# spaces with no further parsing. At the same time GCC is bad in
# argument handling, so let's help it a bit.
separate_arguments(TEST_C_FLAGS UNIX_COMMAND "${TARGET_C_FLAGS} ${HOST_CFLAGS}")

execute_process(
  COMMAND ${CMAKE_C_COMPILER} ${TEST_C_FLAGS} -E lj_arch.h -dM
  WORKING_DIRECTORY ${LUAJIT_SOURCE_DIR}
  OUTPUT_VARIABLE TESTARCH
)

foreach(TRYARCH X64 X86 ARM ARM64 PPC MIPS64 MIPS)
  string(FIND "${TESTARCH}" "LJ_TARGET_${TRYARCH}" FOUND)
  if(FOUND EQUAL -1)
    continue()
  endif()
  set(LUAJIT_ARCH ${TRYARCH})
  string(TOLOWER ${LUAJIT_ARCH} LUAJIT_ARCH)
  break()
endforeach()

if(NOT LUAJIT_ARCH)
  message(FATAL_ERROR "[SetDynASMFlags] Unsupported target architecture")
endif()

append_flags(HOST_C_FLAGS -DLUAJIT_TARGET=LUAJIT_ARCH_${LUAJIT_ARCH})

# XXX: There are few exceptions to the rule.
if(LUAJIT_ARCH STREQUAL "x64")
  string(FIND TESTARCH "LJ_FR2 1" FOUND)
  if(FOUND EQUAL -1)
    set(DYNASM_ARCH x86)
  else()
    set(DYNASM_ARCH x64)
  endif()
elseif(LUAJIT_ARCH STREQUAL "ppc")
  string(FIND TESTARCH "LJ_TARGET_PPC64" FOUND)
  if(FOUND EQUAL -1)
    set(DYNASM_ARCH ppc)
  else()
    set(DYNASM_ARCH ppc64)
  endif()
else()
  set(DYNASM_ARCH ${LUAJIT_ARCH})
endif()

message(STATUS "!WIP: ${LUAJIT_ARCH} => ${DYNASM_ARCH}")

# Set custom additionals defines.
if(LUAJIT_ARCH STREQUAL "arm" AND TARGET_SYS STREQUAL "iOS")
  list(APPEND DYNASM_FLAGS -D IOS)
endif()

if(LUAJIT_ARCH STREQUAL "arm64")
  string(FIND TESTARCH "__AARCH64EB__" FOUND)
  if(NOT FOUND EQUAL -1)
    append_flags(HOST_C_FLAGS -D__AARCH64EB__=1)
  endif()
elseif(LUAJIT_ARCH STREQUAL "ppc")
  string(FIND TESTARCH "LJ_LE 1" FOUND)
  if(NOT FOUND EQUAL -1)
    append_flags(HOST_C_FLAGS -DLJ_ARCH_ENDIAN=LUAJIT_LE)
  else()
    append_flags(HOST_C_FLAGS -DLJ_ARCH_ENDIAN=LUAJIT_BE)
  endif()
elseif(LUAJIT_ARCH STREQUAL "mips")
  string(FIND TESTARCH "MIPSEL" FOUND)
  if(NOT FOUND EQUAL -1)
    append_flags(HOST_C_FLAGS -D__MIPSEL__=1)
  endif()
endif()

string(FIND "${TESTARCH}" "LJ_LE 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D ENDIAN_LE)
else()
  list(APPEND DYNASM_FLAGS -D ENDIAN_BE)
endif()

string(FIND "${TESTARCH}" "LJ_ARCH_BITS 64" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D P64)
endif()

string(FIND "${TESTARCH}" "LJ_HASJIT 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D JIT)
endif()

string(FIND "${TESTARCH}" "LJ_HASFFI 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D FFI)
endif()

string(FIND "${TESTARCH}" "LJ_DUALNUM 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D DUALNUM)
endif()

string(FIND "${TESTARCH}" "LJ_ARCH_HASFPU 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D FPU)
  append_flags(HOST_C_FLAGS -DLJ_ARCH_HASFPU=1)
else()
  append_flags(HOST_C_FLAGS -DLJ_ARCH_HASFPU=0)
endif()

string(FIND "${TESTARCH}" "LJ_ABI_SOFTFP 1" FOUND)
if(NOT FOUND EQUAL -1)
  append_flags(HOST_C_FLAGS -DLJ_ABI_SOFTFP=1)
else()
  list(APPEND DYNASM_FLAGS -D HFABI)
  append_flags(HOST_C_FLAGS -DLJ_ABI_SOFTFP=0)
endif()

string(FIND "${TESTARCH}" "LJ_NO_UNWIND 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D NO_UNWIND)
  append_flags(HOST_C_FLAGS -DLUAJIT_NO_UNWIND)
endif()

string(REGEX MATCH "LJ_ARCH_VERSION ([0-9]+)" LUAJIT_ARCH_VERSION ${TESTARCH})
list(APPEND DYNASM_FLAGS -D VER=${CMAKE_MATCH_1})
message(STATUS "!WIP: DYNASM_FLAGS=${DYNASM_FLAGS}")

if(NOT CMAKE_SYSTEM_NAME STREQUAL ${CMAKE_HOST_SYSTEM_NAME})
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    append_flags(HOST_C_FLAGS -DLUAJIT_OS=LUAJIT_OS_OSX)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    append_flags(HOST_C_FLAGS -DLUAJIT_OS=LUAJIT_OS_WINDOWS) #TODO: -malign-double)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    append_flags(HOST_C_FLAGS -DLUAJIT_OS=LUAJIT_OS_LINUX)
  else()
    append_flags(HOST_C_FLAGS -DLUAJIT_OS=LUAJIT_OS_OTHER)
  endif()
endif()

unset(LUAJIT_ARCH)
unset(TESTARCH)
