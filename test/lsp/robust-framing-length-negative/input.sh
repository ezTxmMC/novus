# A negative Content-Length: same answer, same end.
source "$LSP_LIB"
initialize_frame 1
raw 'Content-Length: -5\r\n\r\n'
frame '{"jsonrpc":"2.0","id":2,"method":"nope/after"}'
