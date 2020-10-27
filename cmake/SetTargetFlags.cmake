# This module exposes following variables to the project:
# * HOST_C_FLAGS
# * DYNASM_ARCH
# * DYNASM_FLAGS

# XXX: <execute_process> simply splits the COMMAND argument by
# spaces with no further parsing. At the same time GCC is bad in
# argument handling, so let's help it a bit.
separate_arguments(TEST_C_FLAGS UNIX_COMMAND "${TARGET_C_FLAGS}")

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

# Target-specific compiler options.
# x86/x64 only: For GCC 4.2 or higher and if you don't intend to
# distribute the binaries to a different machine you could also
# use: -march=native.
if(LUAJIT_ARCH STREQUAL "x86")
  append_flags(TARGET_C_FLAGS -march=i686 -msse -msse2 -mfpmath=sse)
endif()

if(LUAJIT_ARCH STREQUAL "arm64" AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  append_flags(TARGET_C_FLAGS -fno-omit-frame-pointer)
endif()

if(LUAJIT_ARCH STREQUAL "x64" AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  append_flags(TARGET_BIN_FLAGS -pagezero_size 10000 -image_base 100000000)
  append_flags(TARGET_SHARED_FLAGS -image_base 7fff04c4a000)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
  append_flags(TARGET_SHARED_FLAGS -single_module -undefined dynamic_lookup)
endif()

if(NOT LUAJIT_ARCH)
  message(FATAL_ERROR "[SetTargetFlags] Unsupported target architecture")
endif()

# # Auxilary flags for the VM core. Clang warns us explicitly that these flags
# # are unused. Other compilers, however, are silent, so we still set the flags
# # in order not to break anything.
# if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
#   set(TARGET_VM_FLAGS "")
# else()
set(TARGET_VM_FLAGS "${TARGET_C_FLAGS}")
# endif()

# ifneq (,$(findstring LJ_TARGET_PS3 1,$(TARGET_TESTARCH)))
#   TARGET_SYS= PS3
#   TARGET_C_FLAGS_LIST+= -D__CELLOS_LV2__
#   TARGET_XCFLAGS+= -DLUAJIT_USE_SYSMALLOC
#   TARGET_XLIBS+= -lpthread
# endif

unset(TESTARCH)
