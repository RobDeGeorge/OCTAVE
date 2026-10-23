#!/bin/bash
# Copies the built SDK next to the page (a --web-root serves only its own
# folder), then the app can serve it:
#   ./build/octave --service --web-root sdk/typescript/examples/first-frontend
set -e
cd "$(dirname "$0")"
npm --prefix ../.. run build > /dev/null
rm -rf lib && mkdir lib && cp ../../dist/*.js lib/
echo "ready: ./build/octave --service --web-root $(pwd)"
