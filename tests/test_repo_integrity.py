"""Repository integrity checks - GitHub Actions proves the deliverables exist.

These tests validate that every artefact promised in the README is actually
committed: the vision/demo photo, the PCB photo, the breadboard photo, the
Fritzing source (and that it really contains the documented parts), the
firmware, and the CI workflow itself.
"""

import re
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REQUIRED_FILES = [
    "README.md",
    "LICENSE",
    "pytest.ini",
    "requirements-dev.txt",
    ".github/workflows/ci.yml",
    "docs/research.md",
    "docs/media/README.md",
    "docs/media/vision-demo.png",
    "docs/media/poi.png",
    "docs/media/pcb-design.png",
    "docs/media/breadboard-prototype.png",
    "hardware/360_degree_solar_panel_detector.fzz",
    "hardware/README.md",
    "firmware/solar_tracker/solar_tracker.ino",
    "firmware/libraries/tracking_logic/tracking_logic.h",
    "firmware/libraries/tracking_logic/tracking_logic.c",
    "firmware/libraries/tracking_logic/library.properties",
    "software/tracker_sim.py",
    "tests/test_tracking_logic.c",
    "tests/test_tracker_sim.py",
]

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def test_all_required_files_exist():
    missing = [p for p in REQUIRED_FILES if not (ROOT / p).is_file()]
    assert not missing, f"missing files: {missing}"


def test_photos_are_real_pngs():
    for name in ("vision-demo.png", "poi.png", "pcb-design.png", "breadboard-prototype.png"):
        data = (ROOT / "docs" / "media" / name).read_bytes()[:8]
        assert data == PNG_MAGIC, f"{name} is not a valid PNG"


def test_fritzing_design_is_valid_and_matches_bom():
    fzz = ROOT / "hardware" / "360_degree_solar_panel_detector.fzz"
    assert zipfile.is_zipfile(fzz), "fzz must be a zip archive"

    with zipfile.ZipFile(fzz) as zf:
        fz_names = [n for n in zf.namelist() if n.endswith(".fz")]
        assert fz_names, "fzz must contain a .fz document"
        root = ET.fromstring(zf.read(fz_names[0]))

    counts, resistor_values, servo_paths = {}, set(), set()
    for inst in root.iter("instance"):
        module = inst.get("moduleIdRef") or inst.get("moduleid") or ""
        counts[module] = counts.get(module, 0) + 1
        if module == "lijaenargnn-ResistorModuleID":
            for prop in inst.findall("property"):
                if prop.get("name") == "resistance":
                    resistor_values.add(prop.get("value"))
        if inst.get("path"):
            servo_paths.add(Path(inst.get("path")).name.lower())

    assert counts.get("2010BBCD20113PhotocellModuleID") == 4, "4 quadrant LDRs"
    assert counts.get("lijaenargnn-ResistorModuleID") == 4, "4 divider resistors"
    assert counts.get("generic_small_6V_solar_panel_2") == 1, "6V solar panel"
    assert counts.get("3234DBDC80leg") == 2, "2 servos (pan + incline)"
    assert resistor_values == {"10k"}, f"expected 10k dividers, got {resistor_values}"
    assert "servo.fzp" in servo_paths, "the two J parts must be servos"


def test_readme_references_every_image():
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for img in ("vision-demo.png", "poi.png", "pcb-design.png", "breadboard-prototype.png"):
        assert img in readme, f"README must embed {img}"
        assert (ROOT / "docs" / "media" / img).is_file()


def test_readme_contains_required_sections():
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    required_phrases = [
        "AgriStorage",                    # sister storage-house project
        "youtube.com/watch?v=zXX4d5eyvb8",  # the build video credit
        "SolidWorks",                     # mechanical design tool
        "Cameroon",                       # deployment context
        "incline",                        # bidirectional incline axis
        "360",                            # continuous azimuth
        "crop storage",                   # farm storage use case
        "school",                         # school electrification use case
        "actions/workflows/ci.yml",       # CI badge
        "360_degree_solar_panel_detector.fzz",  # fritzing source link
        "10 k",                           # BOM resistor value
    ]
    missing = [p for p in required_phrases if p not in readme]
    assert not missing, f"README missing: {missing}"


def test_readme_image_links_resolve():
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for rel in re.findall(r"!\[[^\]]*\]\(([^)]+)\)", readme):
        if rel.startswith(("http://", "https://")):
            continue
        target = (ROOT / rel.split("#")[0]).resolve()
        assert target.is_file(), f"broken README image link: {rel}"


def test_research_doc_has_citations():
    research = (ROOT / "docs" / "research.md").read_text(encoding="utf-8")
    links = re.findall(r"https?://[^\s)\]]+", research)
    assert len(links) >= 8, "research must cite at least 8 sources"
    assert "FAO" in research
    assert "World Bank" in research or "worldbank" in research.lower()


def test_ci_workflow_runs_every_suite():
    wf = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    for needle in ("test_tracking_logic", "pytest", "arduino-cli"):
        assert needle in wf, f"CI must run {needle}"
