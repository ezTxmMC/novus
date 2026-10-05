#!/usr/bin/env bash
# A 10 002 line file is opened, edited incrementally near its end and its start, and replaced (DESIGN 9.5-6, owner G).
source "$LSP_LIB"

uri="$ROOTURI/big.nv"
body="$(awk 'BEGIN { printf "package main\\n\\n"; for (i = 1; i <= 2500; i++) { printf "method f%d() {\\n    return\\n}\\n\\n", i } }')"

initialize_frame 1
frame '{"jsonrpc":"2.0","method":"initialized","params":{}}'
frame "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"$uri\",\"languageId\":\"novus\",\"version\":1,\"text\":\"$body\"}}}"
frame "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"$uri\",\"version\":2},\"contentChanges\":[{\"range\":{\"start\":{\"line\":8999,\"character\":10},\"end\":{\"line\":8999,\"character\":10}},\"text\":\"#\"}]}}"
frame "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"$uri\",\"version\":3},\"contentChanges\":[{\"range\":{\"start\":{\"line\":2,\"character\":0},\"end\":{\"line\":2,\"character\":0}},\"text\":\"method top() {\\n    return\\n}\\n\\n\"}]}}"
frame "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":\"$uri\",\"version\":4},\"contentChanges\":[{\"text\":\"package main\\n\"}]}}"
frame "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":\"$uri\"}}}"
shutdown_frames 2
