#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
javac --release 11 -d "$test_dir" \
    "$repo_dir/android/app/src/main/java/io/github/pellcorp/grumpyscreen/PrinterConfig.java" \
    "$repo_dir/android/tests/PrinterConfigTest.java"
java -cp "$test_dir" io.github.pellcorp.grumpyscreen.PrinterConfigTest "$repo_dir/grumpyscreen.cfg"
