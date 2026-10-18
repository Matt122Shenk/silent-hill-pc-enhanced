#!/usr/bin/env sh
# nxdk-link with the argument list in a response file.
#
# nxdk-link execs the native lld with every object on its command line, and
# MSYS has to flatten that argv into ONE Windows command line, capped at 32767
# characters. The PC port's newer sources pushed the xbe's link past it
# ("lld: Argument list too long"). lld reads a response file instead; the only
# work is turning MSYS paths (/c/x, -out:/c/x, -wholearchive:/c/x) into the
# C:/x form a native program understands, since nothing converts them inside
# a file.
set -e

rsp="${TMPDIR:-/tmp}/sh_xbox_link.$$.rsp"
: > "$rsp"
for a in "$@"; do
    printf '"%s"\n' "$(printf '%s' "$a" | sed -E 's#^(-[A-Za-z]+:)?/([A-Za-z])/#\1\2:/#')" >> "$rsp"
done

rc=0
lld -flavor link \
    -subsystem:windows \
    -fixed \
    -base:0x00010000 \
    -stack:65536 \
    -merge:.edata=.edataxb \
    "@$(cygpath -m "$rsp")" || rc=$?
rm -f "$rsp"
exit $rc
