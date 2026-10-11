#!/usr/bin/env python3
"""av-cli field smoke: box scenario + fieldGrid produces field.json.

Stdlib only. Run: cli_field_smoke.py <av-cli-binary> <box-scenario.json>
"""

import json
import os
import subprocess
import sys
import tempfile


def fail(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def expect(cond, msg):
    if not cond:
        fail(msg)


def run_cli(binary, scenario, fixtures_dir, tmp, out_name, field_grid):
    scenario = json.loads(json.dumps(scenario))
    scenario["room"]["mesh"]["uri"] = os.path.join(fixtures_dir, "box.obj")
    scenario["params"]["methodParams"]["rayCount"] = 300
    scenario["params"]["methodParams"]["maxBounces"] = 12
    if field_grid is None:
        scenario["params"].pop("fieldGrid", None)
    else:
        scenario["params"]["fieldGrid"] = field_grid
    scenario_file = os.path.join(tmp, f"{out_name}-scenario.json")
    out_dir = os.path.join(tmp, out_name)
    with open(scenario_file, "w") as f:
        json.dump(scenario, f)
    proc = subprocess.run(
        [binary, "run", scenario_file, "-o", out_dir],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        fail(f"av-cli run failed ({out_name}): {proc.stderr}")
    return out_dir


def main():
    if len(sys.argv) != 3:
        fail("usage: cli_field_smoke.py <av-cli-binary> <box-scenario.json>")
    binary, scenario_path = sys.argv[1], sys.argv[2]
    fixtures_dir = os.path.dirname(os.path.abspath(scenario_path))
    with open(scenario_path) as f:
        scenario = json.load(f)

    with tempfile.TemporaryDirectory() as tmp:
        # Enabled: field.json written with the full field shape.
        out_dir = run_cli(
            binary, scenario, fixtures_dir, tmp, "field-on",
            {"enabled": True, "nx": 6, "ny": 4, "nz": 5},
        )
        field_path = os.path.join(out_dir, "field.json")
        expect(os.path.isfile(field_path), "field.json not written")
        with open(field_path) as f:
            field = json.load(f)
        for key in ("nx", "ny", "nz", "origin", "cellSize", "bandHz", "unit", "values"):
            expect(key in field, f"field.json missing {key}")
        expect(field["nx"] == 6 and field["ny"] == 4 and field["nz"] == 5,
               f"dims: {field['nx'], field['ny'], field['nz']}")
        expect(len(field["values"]) == 6 * 4 * 5,
               f"values length {len(field['values'])}")
        expect(field["unit"] == "energy", f"unit {field['unit']}")
        expect(all(v == v for v in field["values"]), "field values not finite")
        expect(any(v > 0 for v in field["values"]), "field values all zero")
        expect(os.path.isfile(os.path.join(out_dir, "metrics.json")),
               "metrics.json missing")
        expect(os.path.isfile(os.path.join(out_dir, "ir.wav")), "ir.wav missing")
        print("ok: field.json written when fieldGrid enabled")

        # Disabled (default): no field.json, other artifacts unchanged.
        out_dir = run_cli(binary, scenario, fixtures_dir, tmp, "field-off", None)
        expect(not os.path.isfile(os.path.join(out_dir, "field.json")),
               "field.json written without fieldGrid")
        expect(os.path.isfile(os.path.join(out_dir, "metrics.json")),
               "metrics.json missing (disabled run)")
        expect(os.path.isfile(os.path.join(out_dir, "ir.wav")),
               "ir.wav missing (disabled run)")
        print("ok: field.json absent when fieldGrid disabled")

    print("ALL CLI FIELD SMOKE TESTS PASSED")


if __name__ == "__main__":
    main()
