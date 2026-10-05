# Content-Length: 0 is an empty frame and ignored; the request behind it is answered normally.
source "$LSP_LIB"
raw 'Content-Length: 0\r\n\r\n'
initialize_frame 1
raw 'Content-Length: 0\r\n\r\n'
shutdown_frames 2
