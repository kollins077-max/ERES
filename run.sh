#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "Usage: ./run.sh MODE [INITIAL_FRACTION UPDATE_EDGES]" >&2
    exit 1
fi

make

binary="./FINDSCC"
if [ ! -x "$binary" ] && [ -x "./FINDSCC.exe" ]; then
    binary="./FINDSCC.exe"
fi

exec "$binary" graph.txt query.txt output.txt "$@"
