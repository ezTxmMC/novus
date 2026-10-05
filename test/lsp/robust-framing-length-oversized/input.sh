# One byte above MAX_FRAME_BYTES (64 MiB): refused before a single body byte is read.
source "$LSP_LIB"
initialize_frame 1
raw 'Content-Length: 67108865\r\n\r\n'
frame '{"jsonrpc":"2.0","id":2,"method":"nope/after"}'
