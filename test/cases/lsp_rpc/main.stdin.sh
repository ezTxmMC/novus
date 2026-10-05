#!/usr/bin/env bash
# The bytes the reader thread of the case reads: requests, notifications, a cancellation, bad JSON, a zero
# frame, a response, a header block without Content-Length (which ends the stream), and a frame that must
# never be read.
frame() {
    printf 'Content-Length: %s\r\n\r\n%s' "$(printf '%s' "$1" | LC_ALL=C wc -c | tr -d ' ')" "$1"
}
frame '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
frame '{"jsonrpc":"2.0","method":"initialized","params":{}}'
frame '{"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":1}}'
frame '{"jsonrpc":"2.0","id":"1","method":"textDocument/hover","params":{}}'
frame '{not json'
frame '[1,2]'
frame 'null'
printf 'Content-Length: 0\r\n\r\n'
frame '{"jsonrpc":"2.0","id":"novus-1","result":null}'
frame '{"jsonrpc":"2.0","method":"$/cancelRequest","params":5}'
frame '{"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":77}}'
printf 'Content-Type: x\r\n\r\n'
frame '{"jsonrpc":"2.0","id":2,"method":"never/read"}'
