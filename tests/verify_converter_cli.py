"""Noninteractive production-CLI integration test; never edits source assets."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def main(repo, generated):
    cli = repo / "x64" / "Release" / "vwt-convert.exe"
    run_dir = Path(tempfile.mkdtemp(prefix="cli_pipeline_", dir=generated))

    def run(*args, code=0):
        completed = subprocess.run([str(cli), *map(str, args)], capture_output=True, encoding="utf-8", timeout=180)
        if completed.returncode != code:
            raise AssertionError(f"{args}: exit {completed.returncode}, expected {code}\n{completed.stdout}\n{completed.stderr}")
        if code == 1:
            assert "ERROR:" in completed.stderr
            return completed
        return json.loads(completed.stdout)

    source = generated / "static_import.glb"
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    inspected = run("inspect-glb", source)
    assert len(inspected["parts"]) == 2 and inspected["source_sha256"] == before
    project = run_dir / "automated.vwtimport.xml"
    run("create", source, project, "WEAPON_CARBINERIFLE", "w_ar_cli_test")
    assert not run("validate", project, "assignments", code=2)["valid"]
    run("roles", project, "--suggest")
    assert run("validate", project, "assignments")["valid"]
    run("align", project, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, "--confirm")
    run("pivot", project, 2, 9, -30, 20, 0, 0, 1, "--confirm")
    run("reference", project, repo / "test" / "gen8" / "w_ar_carbinerifle_m31.ydr")
    run("bind", project, "--suggest")
    run("bone-origin", project, 6, 0.5, 0, 0.1)
    assert run("validate", project, "rig")["valid"]
    configured = run("inspect-project", project)
    assert [p["bone"] for p in configured["parts"]] == [2, 14]
    assert len(configured["bones"]) == 17
    assert configured["alignment"]["confirmed"]
    saved = project.read_bytes()
    run("roles", project, 999, "body", code=1)
    assert saved == project.read_bytes(), "Failed mutation must preserve the project."
    destination = run_dir / "published"
    published = run("export", project, destination, "cli_test", "WEAPON_CLI_TEST", 64)
    assert published["in_game_validated"] is False
    for resource, versions in [("cli_test", (165, 13)), ("cli_test_enhanced", (159, 5))]:
        root = destination / resource
        assert (root / "fxmanifest.lua").is_file()
        assert (root / "cl_weaponNames.lua").is_file()
        assert (root / "vwt_validate.lua").is_file()
        assert "client_script 'vwt_validate.lua'" in (root / "fxmanifest.lua").read_text()
        info = ET.parse(root / "meta" / "weapons.meta").getroot()
        names = [n.text for n in info.iter("Name")]
        models = [n.text for n in info.iter("Model")]
        assert "WEAPON_CLI_TEST" in names and "w_ar_cli_test" in models
        for extension, version in zip(("ydr", "ytd"), versions):
            asset = root / "stream" / f"w_ar_cli_test.{extension}"
            magic, actual = struct.unpack_from("<II", asset.read_bytes())
            assert magic == 0x37435352 and actual == version
            component = root / "stream" / f"w_ar_cli_test_mag1.{extension}"
            assert struct.unpack_from("<II", component.read_bytes()) == (0x37435352, version)
        components = ET.parse(root / "meta" / "weaponcomponents.meta")
        component_names = [n.text for n in components.iter("Name")]
        assert "COMPONENT_W_AR_CLI_TEST_MAGAZINE" in component_names
        assert any(n.text == "WAPClip" for n in components.iter("WeaponAttachBone"))
        assert any(n.text == "Gun_Root" for n in components.iter("AttachBone"))
        assert all(n.text != "AAPClip" for n in components.iter("AttachBone"))
        assert "COMPONENT_W_AR_CLI_TEST_MAGAZINE" in names
    report = destination / "conversion-report.txt"
    snapshot = hashlib.sha256(report.read_bytes()).hexdigest()
    run("export", project, destination, "cli_test", "WEAPON_CLI_TEST", 64, code=1)
    assert hashlib.sha256(report.read_bytes()).hexdigest() == snapshot, "Existing output must not be overwritten."
    assert not list(run_dir.glob("*.stage-*")), "Owned staging directories must not leak."
    assert hashlib.sha256(source.read_bytes()).hexdigest() == before, "Source GLB must remain unchanged."
    # Optional opaque scopes use the same real-anchor component path.
    run("roles", project, 2, "scope")
    run("bind", project, "--suggest")
    scope_destination = run_dir / "scope_published"
    run("export", project, scope_destination, "cli_scope", "WEAPON_CLI_SCOPE", 64)
    scope_meta = ET.parse(scope_destination / "cli_scope" / "meta" / "weaponcomponents.meta")
    assert any(n.text == "WAPScop" for n in scope_meta.iter("WeaponAttachBone"))
    assert (scope_destination / "cli_scope" / "stream" / "w_ar_cli_test_scope.ydr").is_file()
    # Do not fabricate helper bones for unsupported specialized components.
    run("roles", project, 2, "flashlight")
    run("bind", project, "--suggest")
    blocked = run_dir / "unsupported_flashlight"
    run("export", project, blocked, "cli_flash", "WEAPON_CLI_FLASH", 64, code=1)
    assert not blocked.exists()
    print(f"Production CLI project/configuration/validation/dual-generation resource test passed: {destination}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: python tests\\verify_converter_cli.py <repo-root> <generated-directory>")
    main(*[Path(value).resolve() for value in sys.argv[1:]])
