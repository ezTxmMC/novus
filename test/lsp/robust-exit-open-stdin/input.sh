# The client ends the session with shutdown and exit and then keeps the pipe open (a real client waits for the
# process to end before it closes anything). The server must exit by itself within the limit of this scenario.
source "$LSP_LIB"
initialize_frame 1
shutdown_frames 2
sleep 4
