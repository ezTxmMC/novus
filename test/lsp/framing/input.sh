# Well-formed frames at their edges: an extra Content-Type header (ignored), a body with multi-byte
# characters whose Content-Length counts bytes, an unusual header spelling and two frames in one write.
source "$LSP_LIB"
initialize_frame 1
raw 'Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n'
frame '{"jsonrpc":"2.0","id":2,"method":"nope/größe/日本語/😀"}'
frame_as 'content-length' '{"jsonrpc":"2.0","id":3,"method":"nope/spelling"}'
{ frame '{"jsonrpc":"2.0","id":4,"method":"nope/first"}'; frame '{"jsonrpc":"2.0","id":5,"method":"nope/second"}'; }
shutdown_frames 6
