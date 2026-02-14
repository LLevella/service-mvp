#!/bin/sh
set -eu

SERVICE_BIN=${SERVICE_BIN:-./service-mvp}
tmp=$(mktemp -d)

cleanup() {
	rm -rf "$tmp"
}
trap cleanup EXIT INT TERM

mkdir -p "$tmp/in/sub" "$tmp/quarantine"
touch "$tmp/in/test1.pdf"
touch "$tmp/in/test2.doc"
touch "$tmp/in/keep.txt"
touch "$tmp/in/sub/test3.xlsx"

dry_output=$("$SERVICE_BIN" \
	--mark-dir "$tmp/in" \
	--remove-dir "$tmp/in" \
	--quarantine-dir "$tmp/quarantine" \
	--once)

printf '%s\n' "$dry_output" | grep -q 'matches(mark=1 remove=3)'
test -f "$tmp/in/test1.pdf"
test -f "$tmp/in/test2.doc"
test -f "$tmp/in/sub/test3.xlsx"
test ! -e "$tmp/in/test1.pdf_LtH4Dk"

apply_output=$("$SERVICE_BIN" \
	--mark-dir "$tmp/in" \
	--remove-dir "$tmp/in" \
	--quarantine-dir "$tmp/quarantine" \
	--apply \
	--once)

printf '%s\n' "$apply_output" | grep -q 'applied(marked=1 removed=0 quarantined=3)'
test -f "$tmp/in/keep.txt"
test -f "$tmp/in/test1.pdf_LtH4Dk"
test ! -e "$tmp/in/test1.pdf"
test ! -e "$tmp/in/test2.doc"
test ! -e "$tmp/in/sub/test3.xlsx"
find "$tmp/quarantine" -type f -name '*-test1.pdf' | grep -q .
find "$tmp/quarantine" -type f -name '*-test2.doc' | grep -q .
find "$tmp/quarantine" -type f -name '*-test3.xlsx' | grep -q .

printf '%s\n' "smoke tests passed"
