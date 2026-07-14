"""Loader for intervals.yaml — the single source of truth for Unicode interval
presets and their script-group tags.

Imported by:
  * fontconvert_sdcard.py     — INTERVAL_PRESETS (name -> [(start, end), ...]).
  * generate-font-manifest.py — scripts_for_presets() / SCRIPT_GROUPS to derive
                                the manifest's per-family `scripts` tags and the
                                top-level `scriptGroups` block.

Keep this module dependency-light (yaml only) so both scripts can import it
without pulling in freetype/fonttools.
"""

from __future__ import annotations

from pathlib import Path

import yaml

_REGISTRY_PATH = Path(__file__).parent / "intervals.yaml"


def _load() -> dict:
    with open(_REGISTRY_PATH) as f:
        data = yaml.safe_load(f)

    groups = data.get("groups", [])
    presets = data.get("presets", {})

    # Validate that every preset's group tag is declared in `groups`. This is the
    # payoff of a single source: a typo'd or undeclared group fails the build
    # instead of silently dropping fonts out of their section on device.
    known_tags = {g["tag"] for g in groups}
    for name, spec in presets.items():
        tag = spec.get("group")
        if tag is not None and tag not in known_tags:
            raise ValueError(
                f"intervals.yaml: preset '{name}' references unknown group "
                f"'{tag}' (declared groups: {', '.join(sorted(known_tags))})"
            )

    return data


_DATA = _load()

# name -> list of (start, end) tuples (inclusive). Consumed by fontconvert.
INTERVAL_PRESETS: dict[str, list[tuple[int, int]]] = {
    name: [tuple(r) for r in spec["ranges"]]
    for name, spec in _DATA["presets"].items()
}

# Ordered list of (tag, label). List order = on-device display order.
SCRIPT_GROUPS: list[tuple[str, str]] = [
    (g["tag"], g["label"]) for g in _DATA.get("groups", [])
]

_GROUP_ORDER = {tag: i for i, (tag, _label) in enumerate(SCRIPT_GROUPS)}

# preset name -> group tag (None for coverage-only presets).
_PRESET_GROUP = {name: spec.get("group") for name, spec in _DATA["presets"].items()}


def scripts_for_presets(preset_names) -> list[str]:
    """Map an iterable of preset names to their ordered, deduplicated script-group
    tags. Coverage-only presets (no group) contribute nothing."""
    tags = set()
    for name in preset_names:
        tag = _PRESET_GROUP.get(name.strip())
        if tag:
            tags.add(tag)
    return sorted(tags, key=lambda t: _GROUP_ORDER.get(t, len(SCRIPT_GROUPS)))
