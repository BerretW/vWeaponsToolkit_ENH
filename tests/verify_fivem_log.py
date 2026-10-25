"""Verify exported resources' nonintrusive FiveM client asset-check records."""
import argparse
import json
from pathlib import Path
import sys


def verify(text, resource):
    matching = []
    for line in text.splitlines():
        if "[vwt-validation] " not in line:
            continue
        payload = line.split("[vwt-validation] ", 1)[1]
        record = json.loads(payload)
        if not isinstance(record, dict):
            raise ValueError("Runtime validation record must be a JSON object")
        if record.get("resource") == resource:
            matching.append(record)
    if not matching:
        raise ValueError(f"No runtime validation record for {resource}")
    result = matching[-1]
    if result.get("weapon_valid") is not True or result.get("asset_loaded") is not True:
        raise ValueError(f"Weapon registration/asset load failed: {result}")
    components = result.get("components")
    count = result.get("component_count")
    if type(count) is not int or count < 0:
        raise ValueError("Missing/invalid component_count")
    if count == 0 and components == {}:
        components = []
    if not isinstance(components, list):
        raise ValueError("Missing component validation list")
    if len(components) != count:
        raise ValueError("Component validation count differs")
    for component in components:
        if not isinstance(component, dict) or component.get("compatible") is not True or component.get("model_matches") is not True or component.get("model_loaded") is not True:
            raise ValueError(f"Component validation failed: {component}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("resource")
    args = parser.parse_args()
    try:
        checked = verify(args.log.read_text(encoding="utf-8", errors="strict"), args.resource)
        print(json.dumps({"runtime_assets_valid": True, "record": checked,
                          "animations_and_appearance_validated": False}))
    except (OSError, UnicodeError, ValueError, TypeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
