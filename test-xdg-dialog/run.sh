#!/bin/bash

# Run the XDG Dialog v1 test client with Wayland backend
SDL_VIDEODRIVER=wayland ./test-xdg-dialog "$@"