# Your first frontend in 60 lines

`app.js` is the walkthrough in `wiki/writing-a-frontend.html`: the TypeScript SDK in a plain page, no framework and no bundler. It connects to the app that serves it, pairs when the browser is not on the head unit, shows SPEED and RPM live (asking the app to poll them), the track with transport, and the cover.

```bash
cd sdk/typescript && npm install && examples/first-frontend/build.sh    # builds the SDK and copies it to lib/
./build/octave --dev --service --web-root sdk/typescript/examples/first-frontend   # a simulated car, page at http://127.0.0.1:9876/
```

Over the network: start with `--service-bind 0.0.0.0`, open the page by the head unit's address, and type the code from Settings, Service, Pair a device.
