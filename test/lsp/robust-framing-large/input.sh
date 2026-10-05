# A frame of about 5 MB (a huge string in initializationOptions) is read, parsed and answered.
source "$LSP_LIB"
filler="$(head -c 5000000 /dev/zero | tr '\0' 'x')"
frame "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"initializationOptions\":{\"filler\":\"$filler\"},\"capabilities\":{}}}"
shutdown_frames 2
