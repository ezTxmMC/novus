#!/usr/bin/env bash
# stdin of io_limits.nv: bodies above the 1 MiB direct read limit, which
# io.readBytes reads into a buffer that grows with what arrives.
#   frame 1: 1500000 announced, 1500000 delivered (one doubling, a full buffer)
#   frame 2: 10 bytes
#   frame 3: 3000000 announced, 2500000 delivered (two doublings, then the end of input)
set -eu
printf '1500000\n'
head -c 1500000 /dev/zero | tr '\0' 'x'
printf '10\n0123456789'
printf '3000000\n'
head -c 2500000 /dev/zero | tr '\0' 'y'
