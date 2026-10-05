#!/usr/bin/env bash
# One frame (or one broken header block) per line of the golden file, in this order. Leading zeros do not count towards the digit limit. The oversized lengths
# are not followed by body bytes. The last block ends in the middle of the headers.
frame() {
    printf 'Content-Length: %s\r\n\r\n%s' "$(printf '%s' "$1" | LC_ALL=C wc -c | tr -d ' ')" "$1"
}
frame '{"a":1}'
printf 'Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n'
frame '{"s":"größe 日本 😀"}'
printf 'content-length:   7\r\n\r\n{"b":2}'
printf 'Content-Length: 0\r\n\r\n'
printf 'Content-Type: text/plain\r\n\r\n'
printf 'Content-Length: abc\r\n\r\n'
printf 'Content-Length: -3\r\n\r\n'
printf 'Content-Length: 12x\r\n\r\n'
printf 'Content-Length: \r\n\r\n'
printf 'Content-Length: 67108865\r\n\r\n'
printf 'Content-Length: 99999999999999999999\r\n\r\n'
printf 'Content-Length: 000000000000000000007\r\n\r\n{"c":3}'
printf 'Content-Length: 0000000000000067108865\r\n\r\n'
printf 'Content-Length: 00000000000000000000\r\n\r\n'
frame $'inside: Content-Length: 3\r\n\r\nxyz'
frame "$(head -c 1048576 /dev/zero | tr '\0' 'x')"
frame '{"last":true}'
printf 'Content-Length: 5\r\nContent-Ty'
