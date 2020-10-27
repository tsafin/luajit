# Find, check and set LuaJIT's version from a VCS tag.
# Major portions taken verbatim or adapted from the uJIT.
# Copyright (C) 2015-2019 IPONWEB Ltd.

function(SetVersion version majver minver patchver)
  # Read version from the project's VCS and store the result into version.
  find_package(Git QUIET REQUIRED)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE vcs_tag)
  string(STRIP "${vcs_tag}" vcs_tag)
  set(${version} ${vcs_tag} PARENT_SCOPE)

  message(STATUS "[SetVersion] Reading version from VCS: ${vcs_tag}")

  # Match version_string against the version regex.
  # Throw an error if it does not match. Otherwise populates variables:
  # * majver: Major version as a number.
  # * minver: Minor version as a number.
  # * relver: Release version as a number.
  # * Optional prerelease suffix.
  # Valid version examples:
  # * v2.0.4-48-gfcc8244
  # * v2.1.0-beta3-57-g2973518
  if(vcs_tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(-(rc|beta)[0-9]+)?-[0-9]+-g[0-9a-z]+$")
    set(${majver} ${CMAKE_MATCH_1} PARENT_SCOPE)
    set(${minver} ${CMAKE_MATCH_2} PARENT_SCOPE)
    set(${relver} ${CMAKE_MATCH_3} PARENT_SCOPE)
  else()
    message(FATAL_ERROR "[SetVersion] Malformed version string '${vcs_tag}'")
  endif()
endfunction()
