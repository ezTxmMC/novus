# A header block without Content-Length while the pipe stays open for two seconds: -32600 is answered
# at once (the reader must not wait for more input before it reports the frame) and the connection ends.
source "$LSP_LIB"
initialize_frame 1
raw 'Content-Type: application/vscode-jsonrpc\r\n\r\n'
sleep 2
