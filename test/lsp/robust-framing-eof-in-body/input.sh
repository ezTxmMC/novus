# A body shorter than announced: the partial body is dropped and nothing is answered for it.
source "$LSP_LIB"
initialize_frame 1
raw 'Content-Length: 100\r\n\r\n{"jsonrpc":"2.0","id":2,'
