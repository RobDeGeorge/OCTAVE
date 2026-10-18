"""Build an isolated Jeep candidate and record reproducible validation evidence.

This is the execution harness for an agent-driven iteration, not an autonomous
geometry optimizer. Each invocation creates a new candidate; it never promotes
assets into the app. See README.md for the review and iteration contract.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/vehicle"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", default="blender")
    parser.add_argument("--python", default=sys.executable, help="Python with numpy")
    parser.add_argument("--node", default="node")
    parser.add_argument("--preview", action="store_true", help="Render four studio views (slower)")
    parser.add_argument("--twin", action="store_true", help="Also build and check the lifted soft-top Blender prototype")
    parser.add_argument("--timeout", type=float, default=1800, help="Per-stage timeout in seconds")
    parser.add_argument("--output", type=Path, help="New, non-existing candidate directory")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = (args.output or ROOT / "dev/jeep-twin/candidates" / stamp).resolve()
    output.mkdir(parents=True, exist_ok=False)
    assets = output / "assets"
    blend = output / "jeep-tj.blend"
    source_paths = sorted(TOOLS.glob("*.py")) + sorted(TOOLS.glob("*.json")) + sorted(TOOLS.glob("*.cjs"))
    source_paths += sorted((ROOT / "frontend").glob("Jeep*"))
    report = {
        "schema_version": 1, "created_utc": stamp, "candidate": str(output),
        "status": "running", "stages": [],
        "source_sha256": {str(p.relative_to(ROOT)): digest(p) for p in source_paths},
        "unverified": ["dimensional fidelity", "full-body collision clearance",
                       "suspension kinematics", "coupled powertrain", "engine internals",
                       "candidate Qt integration", "target-device frame budget"],
    }
    # Preserve the owner specification with each candidate. This records a
    # target, not a claim that the current generator implements it.
    target = json.loads((TOOLS / "target_vehicle.json").read_text())
    (output / "target_vehicle.json").write_text(json.dumps(target, indent=2) + "\n")
    report["target_configuration"] = "target_vehicle.json"
    report["known_target_gaps"] = target["known_generator_gaps"]

    def save():
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")

    def run(name, command):
        log = output / (name + ".log")
        start = time.monotonic()
        stage = {"name": name, "command": [str(x) for x in command], "log": log.name}
        print(f"{name}: {log}", flush=True)
        try:
            with log.open("w") as stream:
                result = subprocess.run(stage["command"], cwd=ROOT, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=args.timeout)
            stage.update(status="passed" if result.returncode == 0 else "failed", returncode=result.returncode)
        except (OSError, subprocess.TimeoutExpired) as error:
            stage.update(status="failed", error=str(error))
        stage["seconds"] = round(time.monotonic() - start, 3)
        report["stages"].append(stage)
        save()
        return stage["status"] == "passed"

    save()
    build = [args.blender, "--background", "--factory-startup", "--python-exit-code", "1",
             "--python", TOOLS / "build_jeep_tj.py", "--", "--output", assets / "jeep_tj_2003.glb",
             "--blend", blend]
    if args.preview and not args.twin:
        build += ["--preview", output / "preview.png"]
    built = run("build", build)
    steering = run("steering", [args.node, TOOLS / "check_jeep_steering.cjs"])
    geometry = panels = False
    if built:
        geometry = run("asset", [args.python, TOOLS / "check_jeep_asset.py", assets])
        panels = run("panels", [args.blender, "--background", "--factory-startup", "--python-exit-code", "1",
                               "--python", TOOLS / "check_jeep_panel_fit.py", "--", "--blend", blend])
    twin_ok = True
    if args.twin:
        twin_ok = False
        if all((built, steering, geometry, panels)):
            command = [args.blender, "--background", "--factory-startup", "--python-exit-code", "1",
                       "--python", TOOLS / "build_jeep_twin.py", "--", "--source", blend,
                       "--output", output / "twin"]
            if args.preview:
                command += ["--render"]
            if run("twin_build", command):
                twin_ok = run("twin_controls", [args.blender, "--background", "--python-exit-code", "1",
                              "--python", TOOLS / "check_jeep_twin.py", "--", "--blend", output / "twin/jeep-twin.blend"])
                fit_ok = run("twin_fit", [args.blender, "--background", "--python-exit-code", "1",
                             "--python", TOOLS / "audit_jeep_fit.py", "--", "--blend", output / "twin/jeep-twin.blend",
                             "--output", output / "twin/fit-audit.json"])
                twin_ok = twin_ok and fit_ok
                if twin_ok and args.preview:
                    twin_ok = run("twin_cabin_preview", [args.blender, "--background", "--python-exit-code", "1",
                                  "--python", TOOLS / "render_jeep_cabin.py", "--", "--blend", output / "twin/jeep-twin.blend",
                                  "--output", output / "twin", "--save-inspection"])
                report["twin_report"] = "twin/report.json"
                report["twin_fit_report"] = "twin/fit-audit.json"
    report["artifacts_sha256"] = {
        str(p.relative_to(output)): digest(p) for p in sorted(output.rglob("*"))
        if p.is_file() and p.suffix in {".glb", ".blend", ".png", ".js", ".json"} and p.name != "report.json"
    }
    report["status"] = ("prototype_checks_passed" if args.twin else "baseline_checks_passed") if all((built, steering, geometry, panels, twin_ok)) else "failed"
    save()
    print(f"{report['status']}: {output / 'report.json'}", flush=True)
    return 1 if report["status"] == "failed" else 0


if __name__ == "__main__":
    raise SystemExit(main())
