# Your own vehicle model

OCTAVE shows a 3D vehicle in the 3D view (Sensors > 3D View) and its picture on the Home screen. Out of the box that is a Jeep TJ. You can put your own car there.

## Adding a model

1. Get a 3D model of your vehicle as **glTF binary (`.glb`)**. Blender exports one with File > Export > glTF 2.0, format "glTF Binary"; most model sites offer a `.glb` download.
2. Settings > Device > Vehicle > **Add a model**, and pick the file.
3. If it comes in sideways or lying down, press **Turn 90°** or **Stand it up** until it sits like a parked car, nose towards you and to the right.

That's all: the model is now the vehicle in the 3D view, and the picture on Home is taken from the preview on that card. Tap another name on the card to switch, **Remove** to delete the one selected. The built-in Jeep is always there.

A model is copied into OCTAVE's own folder, so the file you picked can be moved or deleted afterwards. The limit is 256 MB; a model for a car screen should be far smaller (a few MB draws quickly on a Raspberry Pi, a 100 MB scan does not).

## What a model does and doesn't do

- It is shown whole. It turns with the motion sensor (pitch and roll), you can orbit and zoom it, and it is scaled and centred by itself, so the units of the file don't matter.
- Nothing on it moves. Doors that open on a tap, wheels and driveshafts that turn with the car and working lamps are the built-in Jeep's: they come from a rig made for that model (`tools/vehicle/`), not from the file format.
- A model is data. Nothing in it is run.

## By hand

Each added vehicle is a folder under the config directory (`~/.config/OCTAVE/vehicles/` on Linux, `%APPDATA%/OCTAVE/vehicles/` on Windows, `~/Library/Application Support/OCTAVE/vehicles/` on macOS):

```
vehicles/
  ACTIVE                 the id of the vehicle shown (no file = the built-in Jeep)
  my-civic/
    vehicle.json
    model.glb
    preview.png          written by OCTAVE; delete it to have it rendered again
```

```json
{ "name": "My Civic", "model": "model.glb", "yaw": 0, "tilt": 0 }
```

- The folder's name is the id: lower-case letters, digits and `-`, at most 48 characters.
- `model` is a file in that folder, `.glb` or `.gltf` (a `.gltf` with its `.bin` and textures beside it works here, though not through Add a model, which copies one file).
- `yaw` and `tilt` are degrees: `yaw` turns the model about the vertical, `tilt` tips it about the side-to-side axis. The buttons change them by 90.

A folder is listed at the next start, or the next time the list changes.

## For frontend authors

The service object is `vehicleManager` (experimental): `vehicles`, `activeId`, `active` (`{id, name, builtIn, rigged, modelUrl, pictureUrl, yaw, tilt}`), `importModel(path, name)`, `select(id)`, `remove(id)`, `rename(id, name)`, `setOrientation(id, yaw, tilt)`. See `docs/api/vehicleManager.json`.

The browser demo can't add a model yet (`canImport` is false there).
