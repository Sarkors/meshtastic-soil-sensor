#!/usr/bin/env bash

set -e

# Regenerates ONLY the private Navamesh protobuf classes (PortNum 256 / PRIVATE_APP).
#
# This deliberately does NOT touch protobufs/ (the upstream Meshtastic submodule)
# or src/mesh/generated/meshtastic/. Use bin/regen-protos.sh for those.
#
# Requires nanopb 0.4.9.1 in the firmware root as nanopb-0.4.9/ -- the same
# requirement and the same directory name that bin/regen-protos.sh uses:
#   macOS: curl -sLO https://jpa.kapsi.fi/nanopb/download/nanopb-0.4.9.1-macosx-x86.tar.gz
#   Linux: curl -sLO https://jpa.kapsi.fi/nanopb/download/nanopb-0.4.9.1-linux-x86.tar.gz
#   then:  tar xzf nanopb-0.4.9.1-*.tar.gz && mv nanopb-0.4.9.1-* nanopb-0.4.9
# nanopb* is gitignored, so the toolchain is never committed.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
NANOPB="${NANOPB:-$ROOT/nanopb-0.4.9}"

if [ ! -x "$NANOPB/generator-bin/protoc" ]; then
    echo "error: nanopb generator not found at $NANOPB/generator-bin/protoc" >&2
    echo "       download nanopb 0.4.9.1 (see the comment at the top of this script)," >&2
    echo "       or point NANOPB= at an existing install." >&2
    exit 1
fi

mkdir -p "$ROOT/src/mesh/generated/navamesh"

# nanopb derives both the output path and the #include inside the generated .pb.cpp
# from the proto path relative to -I, so we cd into proto/ and pass the nested
# navamesh/navamesh.proto. That yields src/mesh/generated/navamesh/navamesh.pb.{h,cpp}
# with a self-consistent #include "navamesh/navamesh.pb.h".
# (The nanopb tool also requires any .options file to be resolvable from the CWD.)
cd "$ROOT/proto"
"$NANOPB/generator-bin/protoc" --experimental_allow_proto3_optional \
    "--nanopb_out=-S.cpp -v:$ROOT/src/mesh/generated/" -I=. navamesh/navamesh.proto
