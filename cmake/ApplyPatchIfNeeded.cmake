# Idempotent `git apply` helper used as PATCH_COMMAND for FetchContent.
#
# FetchContent runs PATCH_COMMAND inside the populated source directory.
# That dir is reused across reconfigure / partial reseeds, so calling
# `git apply` blindly would fail with "patch does not apply" once the
# patch is already in place.
#
# We use `git apply --check --reverse` to detect the already-applied
# state (the patch reverse-applies cleanly only when it has already
# been forward-applied) and skip in that case.
#
# Caller must pass:
#   -DPATCH_FILE=<absolute path to the .patch file>

if(NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "ApplyPatchIfNeeded.cmake: PATCH_FILE not provided")
endif()
if(NOT EXISTS "${PATCH_FILE}")
    message(FATAL_ERROR "ApplyPatchIfNeeded.cmake: '${PATCH_FILE}' does not exist")
endif()

find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --reverse "${PATCH_FILE}"
    RESULT_VARIABLE _already_applied
    OUTPUT_QUIET
    ERROR_QUIET)

if(_already_applied EQUAL 0)
    message(STATUS "Patch already applied, skipping: ${PATCH_FILE}")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PATCH_FILE}"
    RESULT_VARIABLE _apply_result
    OUTPUT_VARIABLE _apply_stdout
    ERROR_VARIABLE _apply_stderr)

if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR
        "Failed to apply patch '${PATCH_FILE}'\n"
        "stdout: ${_apply_stdout}\n"
        "stderr: ${_apply_stderr}")
endif()

message(STATUS "Applied patch: ${PATCH_FILE}")
