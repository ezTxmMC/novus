# A Content-Length with leading zeros is a number like any other: 39 bytes however many zeros come first.
source "$LSP_LIB"
initialize_frame 1
printf 'Content-Length: 000000000000000000039\r\n\r\n%s' '{"jsonrpc":"2.0","id":2,"method":"x/y"}'
shutdown_frames 3
