#!/usr/bin/env bash
# Helpers for the input.sh scripts of the protocol scenarios (sourced through $LSP_LIB; see test/run_lsp.sh).
# They write raw bytes to stdout, which the driver pipes into the server.

# frame <json>: one well-formed frame; the length counts bytes, never characters.
frame() {
    local length
    length="$(printf '%s' "$1" | LC_ALL=C wc -c | tr -d ' ')"
    printf 'Content-Length: %s\r\n\r\n%s' "$length" "$1"
}

# raw <text>: the text with \r and \n escapes interpreted, nothing else added.
raw() {
    printf '%b' "$1"
}

# initialize_frame <id>: a minimal initialize request.
initialize_frame() {
    frame "{\"jsonrpc\":\"2.0\",\"id\":$1,\"method\":\"initialize\",\"params\":{\"processId\":null,\"rootUri\":\"$ROOTURI\",\"capabilities\":{}}}"
}

# shutdown_frames <id>: shutdown and exit, the clean end of a session.
shutdown_frames() {
    frame "{\"jsonrpc\":\"2.0\",\"id\":$1,\"method\":\"shutdown\"}"
    frame '{"jsonrpc":"2.0","method":"exit"}'
}

# frame_as <header-name> <json>: a frame whose length header is spelled differently (names are case-insensitive).
frame_as() {
    local length
    length="$(printf '%s' "$2" | LC_ALL=C wc -c | tr -d ' ')"
    printf '%s:   %s\r\n\r\n%s' "$1" "$length" "$2"
}
