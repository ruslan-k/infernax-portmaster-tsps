#!/bin/sh
# Compiler wrapper for the Infernax cross-build.
#
# The recovered baseline is GPL source that must not be edited beyond the adapter
# overlay, but its -Werror build in the pinned container trips two warning classes
# that are unrelated to the port:
#   * warn_unused_result on the runtime's own diagnostic write() calls
#     (glibc headers in this container carry the attribute),
#   * -Wunused-function for helpers included by headers whose call site is
#     compiled out in this configuration.
# Downgrade exactly those two to warnings so every other diagnostic stays fatal.
exec aarch64-linux-gnu-gcc "$@" -DZERO_ZX_GAME=1 -Wno-error=unused-result -Wno-error=unused-function
