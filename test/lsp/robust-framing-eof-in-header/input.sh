# The input ends in the middle of the headers: no output at all, in particular no -32700 and no -32600.
source "$LSP_LIB"
raw 'Content-Length: 40\r\nContent-Ty'
