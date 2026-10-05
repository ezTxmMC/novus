#!/bin/sh
# Counts its runs in a file next to the script and reports the count as the error, so a scenario can see how many runs a command caused.
counter="$(dirname "$0")/runs.txt"
echo run >> "$counter"
count="$(wc -l < "$counter" | tr -d ' ')"
echo "error: $2:1: run $count"
exit 1
