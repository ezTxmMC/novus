# A valid frame written in pieces with pauses in between (inside a header line, between the headers and
# the body, inside the body) is answered like an unsplit one.
source "$LSP_LIB"
body="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"$ROOTURI\",\"capabilities\":{}}}"
length="$(printf '%s' "$body" | LC_ALL=C wc -c | tr -d ' ')"
raw 'Content-Len'
sleep 0.3
raw "gth: $length\r\n"
sleep 0.3
raw '\r\n'
sleep 0.3
printf '%s' "${body:0:20}"
sleep 0.3
printf '%s' "${body:20:30}"
sleep 0.3
printf '%s' "${body:50}"
sleep 0.3
shutdown_frames 2
