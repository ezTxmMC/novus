# A Content-Length that is not a number: -32600 with id null, then the connection ends. The valid
# request behind it is never read.
source "$LSP_LIB"
initialize_frame 1
raw 'Content-Length: twelve\r\n\r\n'
frame '{"jsonrpc":"2.0","id":2,"method":"nope/after"}'
