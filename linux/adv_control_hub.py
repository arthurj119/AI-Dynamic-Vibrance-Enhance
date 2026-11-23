#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AI Dynamic Vibrance Enhance (v2.0.0) - Linux Control Hub & vkBasalt Configurator."""

from __future__ import annotations

import argparse
import copy
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable

APP_VERSION = "2.0.0"
APP_TITLE = "AI Dynamic Vibrance Enhance"
APP_SLUG = "ai-dynamic-vibrance-enhance"
VKBASALT_EFFECT_NAME = "AIDynamicVibrance"
MANAGED_HEADER = "# Managed by AI Dynamic Vibrance Enhance"
TOGGLE_KEYS = ["Home", "Insert", "End", "F9", "F10", "F11", "F12", "Scroll_Lock", "Pause"]

DEFAULT_FILTER_PARAMS: dict[str, Any] = {
    "intensity": 50,
    "saturationBoost": 50,
    "darkLighting": 50,
    "clarity": 50,
    "dynamicHdr": 50,
    "hdrPeakNits": 800,
    "hdrMidGray": 50,
    "brightness": 50,
    "contrast": 50,
    "sharpen": 50,
    "temperature": 50,
    "tint": 50,
    "exposure": 50,
    "gamma": 50,
    "nightMode": 50,
    "blackAndWhite": 100,
    "vignette": 50,
    "colorblindStrength": 50,
    "colorblindType": 0,
    "ambientOcclusion": 50,
    "aoRadius": 50,
    "bloomIntensity": 50,
    "bloomThreshold": 60,
    "filmicStrength": 50,
    "filmicMode": 0,
    "debandStrength": 50,
    "skinProtection": True,
    "adaptiveScene": True,
    "enableDynamicVibrance": True,
    "enableDarkLighting": False,
    "enableAmbientOcclusion": False,
    "enableClarity": False,
    "enableDynamicHdr": False,
    "enableBrightnessContrast": False,
    "enableSharpen": False,
    "enableColorTemp": False,
    "enableGammaExposure": False,
    "enableNightMode": False,
    "enableBlackAndWhite": False,
    "enableVignette": False,
    "enableColorblind": False,
    "enableBloom": False,
    "enableFilmic": False,
    "enableDeband": False,
}

CURATED_PRESETS: dict[str, dict[str, Any]] = {
    "balanced": {
        "name": "Balanced RTX",
        "subtitle": "Calibrated natural vibrance with sky, skin & stone protection",
        "params": copy.deepcopy(DEFAULT_FILTER_PARAMS),
    },
    "cinematic": {
        "name": "Cinematic Film",
        "subtitle": "AgX filmic tone (30%) + Soft Bloom (20%) + AO (30%) + Deband (35%)",
        "params": {
            **copy.deepcopy(DEFAULT_FILTER_PARAMS),
            "intensity": 50,
            "saturationBoost": 50,
            "enableFilmic": True,
            "filmicMode": 1,
            "filmicStrength": 30,
            "enableBloom": True,
            "bloomIntensity": 20,
            "bloomThreshold": 68,
            "enableAmbientOcclusion": True,
            "ambientOcclusion": 30,
            "aoRadius": 50,
            "enableDeband": True,
            "debandStrength": 35,
        },
    },
    "competitive": {
        "name": "Competitive",
        "subtitle": "Dark shadow clarity (35%) + AMD CAS Sharpen+ (45%)",
        "params": {
            **copy.deepcopy(DEFAULT_FILTER_PARAMS),
            "intensity": 60,
            "saturationBoost": 60,
            "enableDarkLighting": True,
            "darkLighting": 35,
            "enableSharpen": True,
            "sharpen": 45,
        },
    },
    "rpg": {
        "name": "Vibrant RPG",
        "subtitle": "Lush open-world color (65/70) + Clarity (25%) + Deband (45%)",
        "params": {
            **copy.deepcopy(DEFAULT_FILTER_PARAMS),
            "intensity": 65,
            "saturationBoost": 70,
            "enableClarity": True,
            "clarity": 25,
            "enableDeband": True,
            "debandStrength": 45,
        },
    },
    "night": {
        "name": "Night Comfort",
        "subtitle": "Relaxed late-night vibrance (40/35) + Blue-light warmth (35%)",
        "params": {
            **copy.deepcopy(DEFAULT_FILTER_PARAMS),
            "intensity": 40,
            "saturationBoost": 35,
            "enableNightMode": True,
            "nightMode": 35,
        },
    },
}

FILMIC_MODES = ["ACES", "AgX", "Technicolor"]
COLORBLIND_TYPES = ["Protanopia (Red)", "Deuteranopia (Green)", "Tritanopia (Blue-Yellow)"]

FILTER_UI_GROUPS: list[dict[str, Any]] = [
    {
        "title": "AI Dynamic Vibrance",
        "enable_key": "enableDynamicVibrance",
        "sliders": [
            ("Intensity (%)", "intensity", 0, 100),
            ("Saturation Boost (%)", "saturationBoost", 0, 100),
        ],
    },
    {
        "title": "Sharpen+ (AMD CAS)",
        "enable_key": "enableSharpen",
        "sliders": [("Sharpness (%)", "sharpen", 0, 100)],
    },
    {
        "title": "Dynamic HDR Tone",
        "enable_key": "enableDynamicHdr",
        "sliders": [
            ("Dynamic HDR Tone (%)", "dynamicHdr", 0, 100),
            ("HDR Mid Gray / Contrast (%)", "hdrMidGray", 0, 100),
        ],
    },
    {
        "title": "Dark / Shadow Lighting",
        "enable_key": "enableDarkLighting",
        "sliders": [("Dark / Shadow Lift (%)", "darkLighting", 0, 100)],
    },
    {
        "title": "Ambient Occlusion (SSAO / Crevice)",
        "enable_key": "enableAmbientOcclusion",
        "sliders": [
            ("AO Intensity (%)", "ambientOcclusion", 0, 100),
            ("AO Radius (%)", "aoRadius", 0, 100),
        ],
    },
    {
        "title": "Clarity / Micro-Contrast",
        "enable_key": "enableClarity",
        "sliders": [("Clarity (%)", "clarity", 0, 100)],
    },
    {
        "title": "Cinematic Bloom",
        "enable_key": "enableBloom",
        "sliders": [
            ("Bloom Intensity (%)", "bloomIntensity", 0, 100),
            ("Bloom Threshold (%)", "bloomThreshold", 0, 100),
        ],
    },
    {
        "title": "Filmic Color Grading",
        "enable_key": "enableFilmic",
        "choice": ("Curve Mode", "filmicMode", FILMIC_MODES),
        "sliders": [("Filmic Strength (%)", "filmicStrength", 0, 100)],
    },
    {
        "title": "Deband (Sky / Shadow Gradient Smoothing)",
        "enable_key": "enableDeband",
        "sliders": [("Deband Strength (%)", "debandStrength", 0, 100)],
    },
    {
        "title": "Brightness / Contrast",
        "enable_key": "enableBrightnessContrast",
        "sliders": [
            ("Brightness (%)", "brightness", 0, 100),
            ("Contrast (%)", "contrast", 0, 100),
        ],
    },
    {
        "title": "Gamma / Exposure",
        "enable_key": "enableGammaExposure",
        "sliders": [
            ("Exposure (%)", "exposure", 0, 100),
            ("Gamma (%)", "gamma", 0, 100),
        ],
    },
    {
        "title": "Color Temperature & Tint",
        "enable_key": "enableColorTemp",
        "sliders": [
            ("Temperature (%)", "temperature", 0, 100),
            ("Tint (%)", "tint", 0, 100),
        ],
    },
    {
        "title": "Night Mode / Blue Light Filter",
        "enable_key": "enableNightMode",
        "sliders": [("Warmth (%)", "nightMode", 0, 100)],
    },
    {
        "title": "Colorblind Assist",
        "enable_key": "enableColorblind",
        "choice": ("Daltonization Type", "colorblindType", COLORBLIND_TYPES),
        "sliders": [("Assist Strength (%)", "colorblindStrength", 0, 100)],
    },
    {
        "title": "Black & White",
        "enable_key": "enableBlackAndWhite",
        "sliders": [("Desaturation Mix (%)", "blackAndWhite", 0, 100)],
    },
    {
        "title": "Cinematic Vignette",
        "enable_key": "enableVignette",
        "sliders": [("Vignette Strength (%)", "vignette", 0, 100)],
    },
]


def _clamp_int(val: Any, lo: int, hi: int, default: int) -> int:
    try:
        num = int(round(float(val)))
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, num))


def _as_bool(val: Any, default: bool) -> bool:
    if isinstance(val, bool):
        return val
    if isinstance(val, (int, float)):
        return val != 0
    if isinstance(val, str):
        low = val.strip().lower()
        if low in ("true", "1", "yes", "on"):
            return True
        if low in ("false", "0", "no", "off"):
            return False
    return default


def normalize_filter_params(raw: dict[str, Any] | None) -> dict[str, Any]:
    """Clamp and sanitize FilterParams with the same rules as Windows ReadParams()."""
    out = copy.deepcopy(DEFAULT_FILTER_PARAMS)
    if not isinstance(raw, dict):
        return out

    int_bounds: dict[str, tuple[int, int]] = {
        "intensity": (0, 100),
        "saturationBoost": (0, 100),
        "darkLighting": (0, 100),
        "clarity": (0, 100),
        "dynamicHdr": (0, 100),
        "hdrPeakNits": (300, 1500),
        "hdrMidGray": (0, 100),
        "brightness": (0, 100),
        "contrast": (0, 100),
        "sharpen": (0, 100),
        "temperature": (0, 100),
        "tint": (0, 100),
        "exposure": (0, 100),
        "gamma": (0, 100),
        "nightMode": (0, 100),
        "blackAndWhite": (0, 100),
        "vignette": (0, 100),
        "colorblindStrength": (0, 100),
        "colorblindType": (0, 2),
        "ambientOcclusion": (0, 100),
        "aoRadius": (0, 100),
        "bloomIntensity": (0, 100),
        "bloomThreshold": (0, 100),
        "filmicStrength": (0, 100),
        "filmicMode": (0, 2),
        "debandStrength": (0, 100),
    }
    for key, (lo, hi) in int_bounds.items():
        if key in raw:
            out[key] = _clamp_int(raw[key], lo, hi, int(out[key]))

    bool_keys = [k for k, v in DEFAULT_FILTER_PARAMS.items() if isinstance(v, bool)]
    for key in bool_keys:
        if key in raw:
            out[key] = _as_bool(raw[key], bool(out[key]))

    out["skinProtection"] = True
    out["adaptiveScene"] = True
    return out


def default_config() -> dict[str, Any]:
    return {
        "schemaVersion": 3,
        "activeProfile": 1,
        "splitPreview": False,
        "splitPosition": 50,
        "toggleKey": "Home",
        "enableOnLaunch": True,
        "depthCapture": False,
        "enableSmaa": False,
        "profiles": [
            copy.deepcopy(DEFAULT_FILTER_PARAMS),
            copy.deepcopy(DEFAULT_FILTER_PARAMS),
            copy.deepcopy(DEFAULT_FILTER_PARAMS),
        ],
        "perGameProfiles": {},
    }


def _xdg_dir(env_name: str, fallback_rel: str, override: Path | None = None) -> Path:
    if override is not None:
        return Path(override)
    env_val = os.environ.get(env_name, "").strip()
    if env_val:
        return Path(env_val)
    return Path.home() / fallback_rel


def get_config_file(xdg_config_home: Path | None = None) -> Path:
    cfg_root = _xdg_dir("XDG_CONFIG_HOME", ".config", xdg_config_home)
    return cfg_root / APP_SLUG / "config.json"


def load_config(xdg_config_home: Path | None = None) -> dict[str, Any]:
    cfg = default_config()
    cfg_path = get_config_file(xdg_config_home)
    if not cfg_path.is_file():
        return cfg
    try:
        data = json.loads(cfg_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return cfg
    if not isinstance(data, dict):
        return cfg

    cfg["activeProfile"] = _clamp_int(data.get("activeProfile", 1), 1, 3, 1)
    cfg["splitPreview"] = _as_bool(data.get("splitPreview", False), False)
    cfg["splitPosition"] = _clamp_int(data.get("splitPosition", 50), 5, 95, 50)
    tkey = str(data.get("toggleKey", "Home")).strip()
    cfg["toggleKey"] = tkey if tkey else "Home"
    cfg["enableOnLaunch"] = _as_bool(data.get("enableOnLaunch", True), True)
    cfg["depthCapture"] = _as_bool(data.get("depthCapture", False), False)
    cfg["enableSmaa"] = _as_bool(data.get("enableSmaa", False), False)

    raw_profiles = data.get("profiles")
    if isinstance(raw_profiles, list):
        for idx in range(min(3, len(raw_profiles))):
            cfg["profiles"][idx] = normalize_filter_params(raw_profiles[idx])

    raw_games = data.get("perGameProfiles")
    if isinstance(raw_games, dict):
        clean_games: dict[str, Any] = {}
        for slug, entry in raw_games.items():
            if not isinstance(slug, str) or not slug.strip() or not isinstance(entry, dict):
                continue
            clean_games[slug.strip()] = {
                "profileSlot": _clamp_int(entry.get("profileSlot", 1), 1, 3, 1),
                "confPath": str(entry.get("confPath", "")),
            }
        cfg["perGameProfiles"] = clean_games
    return cfg


def save_config(cfg: dict[str, Any], xdg_config_home: Path | None = None) -> Path:
    cfg_path = get_config_file(xdg_config_home)
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    with open(cfg_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(cfg, fh, indent=2)
        fh.write("\n")
    return cfg_path


def find_shader_assets() -> tuple[Path, Path]:
    """Locate AIDynamicVibranceEnhance.fx and ReShade.fxh across dev & installed layouts."""
    here = Path(__file__).resolve().parent
    candidate_dirs: list[Path] = []

    env_dir = os.environ.get("ADV_SHADER_DIR", "").strip()
    if env_dir:
        candidate_dirs.append(Path(env_dir))

    candidate_dirs.extend([
        here / "shaders",
        here.parent / "shaders",
        _xdg_dir("XDG_DATA_HOME", ".local/share") / APP_SLUG / "shaders",
        Path.home() / ".local" / "share" / APP_SLUG / "shaders",
        Path("/usr/share") / APP_SLUG / "shaders",
        Path("/usr/local/share") / APP_SLUG / "shaders",
    ])

    fx_path: Path | None = None
    fxh_path: Path | None = None
    for d in candidate_dirs:
        if fx_path is None and (d / "AIDynamicVibranceEnhance.fx").is_file():
            fx_path = d / "AIDynamicVibranceEnhance.fx"
        if fxh_path is None and (d / "ReShade.fxh").is_file():
            fxh_path = d / "ReShade.fxh"

    if fx_path is None:
        raise FileNotFoundError(
            "AIDynamicVibranceEnhance.fx was not found. Checked: "
            + ", ".join(str(p) for p in candidate_dirs)
        )
    if fxh_path is None:
        raise FileNotFoundError(
            "ReShade.fxh was not found. Checked: "
            + ", ".join(str(p) for p in candidate_dirs)
        )
    return fx_path, fxh_path


def uniform_values_from_params(
    params: dict[str, Any],
    *,
    split_preview: bool = False,
    split_position: int = 50,
    depth_capture: bool = False,
) -> dict[str, tuple[str, Any]]:
    """Map FilterParams fields to exact ReShade .fx UI_* uniform declarations."""
    p = normalize_filter_params(params)
    sp = _clamp_int(split_position, 5, 95, 50)
    return {
        "UI_EnableDynamicVibrance": ("bool", bool(p["enableDynamicVibrance"])),
        "UI_Intensity": ("float", int(p["intensity"])),
        "UI_SaturationBoost": ("float", int(p["saturationBoost"])),
        "UI_EnableDarkLighting": ("bool", bool(p["enableDarkLighting"])),
        "UI_DarkLighting": ("float", int(p["darkLighting"])),
        "UI_EnableAO": ("bool", bool(p["enableAmbientOcclusion"])),
        "UI_AOIntensity": ("float", int(p["ambientOcclusion"])),
        "UI_AORadius": ("float", int(p["aoRadius"])),
        "UI_AOUseDepth": ("bool", bool(depth_capture)),
        "UI_EnableDynamicHdr": ("bool", bool(p["enableDynamicHdr"])),
        "UI_DynamicHdr": ("float", int(p["dynamicHdr"])),
        "UI_HdrMidGray": ("float", int(p["hdrMidGray"])),
        "UI_EnableClarity": ("bool", bool(p["enableClarity"])),
        "UI_Clarity": ("float", int(p["clarity"])),
        "UI_EnableBC": ("bool", bool(p["enableBrightnessContrast"])),
        "UI_Brightness": ("float", int(p["brightness"])),
        "UI_Contrast": ("float", int(p["contrast"])),
        "UI_EnableSharpen": ("bool", bool(p["enableSharpen"])),
        "UI_Sharpen": ("float", int(p["sharpen"])),
        "UI_EnableGammaExp": ("bool", bool(p["enableGammaExposure"])),
        "UI_Exposure": ("float", int(p["exposure"])),
        "UI_Gamma": ("float", int(p["gamma"])),
        "UI_EnableColorTemp": ("bool", bool(p["enableColorTemp"])),
        "UI_Temperature": ("float", int(p["temperature"])),
        "UI_Tint": ("float", int(p["tint"])),
        "UI_EnableNightMode": ("bool", bool(p["enableNightMode"])),
        "UI_NightMode": ("float", int(p["nightMode"])),
        "UI_EnableColorblind": ("bool", bool(p["enableColorblind"])),
        "UI_ColorblindType": ("int", int(p["colorblindType"])),
        "UI_ColorblindStrength": ("float", int(p["colorblindStrength"])),
        "UI_EnableBW": ("bool", bool(p["enableBlackAndWhite"])),
        "UI_BW": ("float", int(p["blackAndWhite"])),
        "UI_EnableVignette": ("bool", bool(p["enableVignette"])),
        "UI_Vignette": ("float", int(p["vignette"])),
        "UI_EnableBloom": ("bool", bool(p["enableBloom"])),
        "UI_BloomIntensity": ("float", int(p["bloomIntensity"])),
        "UI_BloomThreshold": ("float", int(p["bloomThreshold"])),
        "UI_EnableFilmic": ("bool", bool(p["enableFilmic"])),
        "UI_FilmicMode": ("int", int(p["filmicMode"])),
        "UI_FilmicStrength": ("float", int(p["filmicStrength"])),
        "UI_EnableDeband": ("bool", bool(p["enableDeband"])),
        "UI_DebandStrength": ("float", int(p["debandStrength"])),
        "UI_SplitPreview": ("bool", bool(split_preview)),
        "UI_SplitPosition": ("float", sp),
    }


def _format_fx_literal(kind: str, val: Any) -> str:
    if kind == "bool":
        return "true" if val else "false"
    if kind == "int":
        return str(int(val))
    return f"{int(val)}.0"


def materialize_shader(
    template_fx_text: str,
    params: dict[str, Any],
    *,
    split_preview: bool = False,
    split_position: int = 50,
    depth_capture: bool = False,
) -> str:
    """Bake active parameters directly into ReShade .fx uniform initializers for vkBasalt."""
    uniform_map = uniform_values_from_params(
        params,
        split_preview=split_preview,
        split_position=split_position,
        depth_capture=depth_capture,
    )
    out = template_fx_text.replace("\r\n", "\n")

    for name, (kind, val) in uniform_map.items():
        pattern = re.compile(
            rf"(uniform\s+(?:bool|float|int)\s+{re.escape(name)}\s*<[^>]*>\s*=\s*)([^;]+)(;)",
            re.MULTILINE,
        )
        matches = pattern.findall(out)
        if len(matches) != 1:
            raise ValueError(f"Expected 1 uniform declaration for {name}, found {len(matches)}")
        literal = _format_fx_literal(kind, val)
        out = pattern.sub(rf"\g<1>{literal}\g<3>", out, count=1)

    return out


def sanitize_slug(text: str) -> str:
    cleaned = re.sub(r"[^a-zA-Z0-9._-]+", "-", text.strip().lower()).strip("-._")
    return cleaned or "custom-game"


def build_vkbasalt_conf_text(
    *,
    shader_path: Path,
    include_dir: Path,
    texture_dir: Path,
    toggle_key: str = "Home",
    enable_on_launch: bool = True,
    depth_capture: bool = False,
    enable_smaa: bool = False,
) -> str:
    """Render a vkBasalt.conf with absolute POSIX paths (never '~')."""
    effects = f"smaa:{VKBASALT_EFFECT_NAME}" if enable_smaa else VKBASALT_EFFECT_NAME
    lines = [
        f"{MANAGED_HEADER} v{APP_VERSION}",
        "# Generated automatically by ai-dynamic-vibrance-enhance. Safe to regenerate anytime.",
        "",
        f"effects = {effects}",
        f"{VKBASALT_EFFECT_NAME} = {shader_path.as_posix()}",
        f"reshadeIncludePath = {include_dir.as_posix()}",
        f"reshadeTexturePath = {texture_dir.as_posix()}",
        "",
        f"toggleKey = {toggle_key}",
        f"enableOnLaunch = {'True' if enable_on_launch else 'False'}",
        f"depthCapture = {'on' if depth_capture else 'off'}",
        "",
    ]
    return "\n".join(lines)


def apply_vkbasalt_config(
    cfg: dict[str, Any],
    *,
    profile_slot: int | None = None,
    game_slug: str | None = None,
    xdg_config_home: Path | None = None,
    xdg_data_home: Path | None = None,
    output_conf_override: Path | None = None,
) -> dict[str, str]:
    """Write materialized .fx shader, ReShade.fxh, and vkBasalt.conf."""
    slot = _clamp_int(profile_slot or cfg.get("activeProfile", 1), 1, 3, 1)
    params = cfg["profiles"][slot - 1]
    split_preview = bool(cfg.get("splitPreview", False))
    split_pos = _clamp_int(cfg.get("splitPosition", 50), 5, 95, 50)
    depth_capture = bool(cfg.get("depthCapture", False))
    enable_smaa = bool(cfg.get("enableSmaa", False))
    enable_on_launch = bool(cfg.get("enableOnLaunch", True))
    toggle_key = str(cfg.get("toggleKey", "Home")).strip() or "Home"

    src_fx, src_fxh = find_shader_assets()
    template_fx = src_fx.read_text(encoding="utf-8")
    materialized_fx = materialize_shader(
        template_fx,
        params,
        split_preview=split_preview,
        split_position=split_pos,
        depth_capture=depth_capture,
    )

    cfg_root = _xdg_dir("XDG_CONFIG_HOME", ".config", xdg_config_home)
    data_root = _xdg_dir("XDG_DATA_HOME", ".local/share", xdg_data_home)
    vk_cfg_dir = cfg_root / "vkBasalt"
    slug = sanitize_slug(game_slug) if game_slug else None

    if slug:
        asset_base = (data_root / "vkBasalt" / "ai-dynamic-vibrance" / "games" / slug).resolve()
        conf_path = (output_conf_override or (vk_cfg_dir / "games" / f"{slug}.conf")).resolve()
    else:
        asset_base = (data_root / "vkBasalt" / "ai-dynamic-vibrance").resolve()
        conf_path = (output_conf_override or (vk_cfg_dir / "vkBasalt.conf")).resolve()

    shader_dir = asset_base / "shaders"
    texture_dir = asset_base / "textures"
    shader_dir.mkdir(parents=True, exist_ok=True)
    texture_dir.mkdir(parents=True, exist_ok=True)
    conf_path.parent.mkdir(parents=True, exist_ok=True)

    dst_fx = (shader_dir / "AIDynamicVibranceEnhance.fx").resolve()
    dst_fxh = (shader_dir / "ReShade.fxh").resolve()

    dst_fx.write_text(materialized_fx, encoding="utf-8", newline="\n")
    dst_fxh.write_text(src_fxh.read_text(encoding="utf-8").replace("\r\n", "\n"), encoding="utf-8", newline="\n")

    backup_path = conf_path.with_name(conf_path.name + ".adv-backup")
    backup_created = False
    if conf_path.is_file():
        existing_head = conf_path.read_text(encoding="utf-8", errors="ignore")[:128]
        if not existing_head.startswith(MANAGED_HEADER) and not backup_path.exists():
            shutil.copy2(conf_path, backup_path)
            backup_created = True

    conf_text = build_vkbasalt_conf_text(
        shader_path=dst_fx,
        include_dir=shader_dir,
        texture_dir=texture_dir,
        toggle_key=toggle_key,
        enable_on_launch=enable_on_launch,
        depth_capture=depth_capture,
        enable_smaa=enable_smaa,
    )
    conf_path.write_text(conf_text, encoding="utf-8", newline="\n")

    if slug:
        launch_opt = f'ENABLE_VKBASALT=1 VKBASALT_CONFIG_FILE="{conf_path.as_posix()}" %command%'
        mango_opt = f'ENABLE_VKBASALT=1 VKBASALT_CONFIG_FILE="{conf_path.as_posix()}" mangohud %command%'
        cfg.setdefault("perGameProfiles", {})[slug] = {
            "profileSlot": slot,
            "confPath": conf_path.as_posix(),
        }
    else:
        launch_opt = "ENABLE_VKBASALT=1 %command%"
        mango_opt = "ENABLE_VKBASALT=1 mangohud %command%"

    save_config(cfg, xdg_config_home)

    return {
        "conf_path": conf_path.as_posix(),
        "shader_path": dst_fx.as_posix(),
        "include_dir": shader_dir.as_posix(),
        "launch_option": launch_opt,
        "mangohud_launch_option": mango_opt,
        "backup_path": backup_path.as_posix() if backup_created else "",
    }


def export_preset_file(params: dict[str, Any], name: str, path: Path) -> Path:
    payload = {
        "format": "AIDynamicVibranceEnhancePreset",
        "version": 3,
        "name": name,
        "params": normalize_filter_params(params),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(payload, fh, indent=2)
        fh.write("\n")
    return path


def import_preset_file(path: Path) -> tuple[str, dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("Invalid preset JSON object")
    name = str(data.get("name") or path.stem)
    if isinstance(data.get("params"), dict):
        return name, normalize_filter_params(data["params"])
    if "intensity" in data or "saturationBoost" in data:
        return name, normalize_filter_params(data)
    raise ValueError("Missing preset parameters")


def detect_linux_environment(xdg_config_home: Path | None = None) -> dict[str, Any]:
    """Inspect OS distro, native/Flatpak vkBasalt installation, and active config."""
    info: dict[str, Any] = {
        "distro_id": "unknown",
        "distro_name": sys.platform,
        "family": "other",
        "immutable": Path("/run/ostree-booted").exists() or not os.access("/usr", os.W_OK),
        "native_layer_json": "",
        "native_64_lib": "",
        "native_32_lib": "",
        "flatpak_installed": False,
        "flatpak_vkbasalt": False,
        "active_conf": "",
        "active_conf_managed": False,
        "install_hint": "Install vkbasalt via your Linux gaming distro package manager.",
    }

    os_release = Path("/etc/os-release")
    if os_release.is_file():
        kv: dict[str, str] = {}
        for line in os_release.read_text(encoding="utf-8", errors="ignore").splitlines():
            if "=" in line and not line.startswith("#"):
                k, v = line.split("=", 1)
                kv[k.strip()] = v.strip().strip('"').strip("'")
        did = kv.get("ID", "unknown").lower()
        like = kv.get("ID_LIKE", "").lower()
        variant = kv.get("VARIANT_ID", "").lower()
        pretty = kv.get("PRETTY_NAME", did)
        info["distro_id"] = did
        info["distro_name"] = pretty

        if did == "steamos" or "steamos" in pretty.lower():
            info["family"] = "steamos"
            info["immutable"] = True
            info["install_hint"] = "SteamOS uses user-space config + Flatpak org.freedesktop.Platform.VulkanLayer.vkBasalt."
        elif did == "bazzite" or "bazzite" in pretty.lower() or (did == "fedora" and Path("/run/ostree-booted").exists()):
            info["family"] = "bazzite"
            info["immutable"] = True
            info["install_hint"] = "Bazzite bundles vkBasalt; click 'Fix Flatpak Permissions' for Flatpak Steam/Lutris."
        elif did in ("arch", "cachyos", "endeavouros", "manjaro", "garuda") or "arch" in like:
            info["family"] = "arch"
            info["install_hint"] = "Arch / CachyOS / EndeavourOS: sudo pacman -S vkbasalt (or yay -S vkbasalt lib32-vkbasalt)"
        elif did in ("fedora", "nobara") or ("fedora" in like and variant not in ("silverblue", "kinoite", "bazzite")):
            info["family"] = "fedora"
            info["install_hint"] = "Fedora / Nobara: sudo dnf install vkBasalt vkBasalt.i686 python3-tkinter"
        elif did in ("ubuntu", "pop", "linuxmint", "debian", "zorin") or "ubuntu" in like or "debian" in like:
            info["family"] = "debian"
            info["install_hint"] = "Ubuntu / Pop!_OS / Mint: sudo apt install vkbasalt python3-tk"

    layer_candidates = [
        Path("/usr/share/vulkan/implicit_layer.d/vkBasalt.json"),
        Path("/usr/local/share/vulkan/implicit_layer.d/vkBasalt.json"),
        Path("/etc/vulkan/implicit_layer.d/vkBasalt.json"),
    ]
    for cand in layer_candidates:
        if cand.is_file():
            info["native_layer_json"] = cand.as_posix()
            break

    lib64_candidates = [
        Path("/usr/lib/libvkbasalt.so"),
        Path("/usr/lib64/libvkbasalt.so"),
        Path("/usr/lib/x86_64-linux-gnu/libvkbasalt.so"),
    ]
    for cand in lib64_candidates:
        if cand.exists():
            info["native_64_lib"] = cand.as_posix()
            break

    lib32_candidates = [
        Path("/usr/lib32/libvkbasalt.so"),
        Path("/usr/lib/libvkbasalt.so"),
        Path("/usr/lib/i386-linux-gnu/libvkbasalt.so"),
    ]
    for cand in lib32_candidates:
        if cand.exists() and cand.as_posix() != info["native_64_lib"]:
            info["native_32_lib"] = cand.as_posix()
            break

    if shutil.which("flatpak"):
        info["flatpak_installed"] = True
        try:
            proc = subprocess.run(
                ["flatpak", "list", "--runtime", "--columns=application"],
                capture_output=True,
                text=True,
                timeout=3,
                check=False,
            )
            info["flatpak_vkbasalt"] = "org.freedesktop.Platform.VulkanLayer.vkBasalt" in proc.stdout
        except OSError:
            pass

    cfg_root = _xdg_dir("XDG_CONFIG_HOME", ".config", xdg_config_home)
    conf_file = cfg_root / "vkBasalt" / "vkBasalt.conf"
    if conf_file.is_file():
        info["active_conf"] = conf_file.as_posix()
        info["active_conf_managed"] = conf_file.read_text(encoding="utf-8", errors="ignore").startswith(MANAGED_HEADER)

    return info


def apply_flatpak_overrides() -> tuple[bool, str]:
    """Grant Flatpak Steam/Heroic/Lutris read access to xdg-config/vkBasalt & xdg-data/vkBasalt."""
    if not shutil.which("flatpak"):
        return False, "flatpak command is not installed on this system."
    try:
        proc = subprocess.run(
            [
                "flatpak",
                "--user",
                "override",
                "--filesystem=xdg-config/vkBasalt:ro",
                "--filesystem=xdg-data/vkBasalt:ro",
            ],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        if proc.returncode == 0:
            return True, "Flatpak permissions updated for xdg-config/vkBasalt and xdg-data/vkBasalt."
        return False, (proc.stderr or proc.stdout or f"flatpak exited {proc.returncode}").strip()
    except OSError as exc:
        return False, str(exc)


def copy_to_clipboard(text: str, tk_root: Any = None) -> str:
    """Copy text using Wayland/X11 CLI clipboard tools or Tkinter fallback."""
    for cmd in (["wl-copy"], ["xclip", "-selection", "clipboard"], ["xsel", "-b", "-i"]):
        if shutil.which(cmd[0]):
            try:
                proc = subprocess.run(cmd, input=text, text=True, timeout=2, check=False)
                if proc.returncode == 0:
                    return cmd[0]
            except OSError:
                pass
    if tk_root is not None:
        tk_root.clipboard_clear()
        tk_root.clipboard_append(text)
        tk_root.update_idletasks()
        return "tkinter"
    return ""


def run_self_tests() -> int:
    """Run deterministic offline checks without touching host user config."""
    checks: list[tuple[str, Callable[[], None]]] = []

    def check_shader_assets() -> None:
        fx_path, fxh_path = find_shader_assets()
        fx_raw = fx_path.read_bytes()
        fxh_raw = fxh_path.read_bytes()
        assert b"\r" not in fx_raw, "AIDynamicVibranceEnhance.fx contains CRLF"
        assert b"\r" not in fxh_raw, "ReShade.fxh contains CRLF"
        fx_txt = fx_raw.decode("utf-8")
        fxh_txt = fxh_raw.decode("utf-8")
        assert '#include "ReShade.fxh"' in fx_txt
        assert "float4 PS_AIDynamicVibrance" in fx_txt
        assert "UI_SplitPreview" in fx_txt and "UI_SplitPosition" in fx_txt
        assert "PostProcessVS" in fxh_txt and "BackBuffer" in fxh_txt

    def check_uniform_substitution() -> None:
        fx_path, _ = find_shader_assets()
        fx_txt = fx_path.read_text(encoding="utf-8")
        mat = materialize_shader(
            fx_txt,
            CURATED_PRESETS["cinematic"]["params"],
            split_preview=True,
            split_position=35,
            depth_capture=False,
        )
        assert re.search(r"UI_EnableFilmic\s*<[^>]*>\s*=\s*true;", mat)
        assert re.search(r"UI_FilmicMode\s*<[^>]*>\s*=\s*1;", mat)
        assert re.search(r"UI_FilmicStrength\s*<[^>]*>\s*=\s*30\.0;", mat)
        assert re.search(r"UI_EnableBloom\s*<[^>]*>\s*=\s*true;", mat)
        assert re.search(r"UI_BloomThreshold\s*<[^>]*>\s*=\s*68\.0;", mat)
        assert re.search(r"UI_AOUseDepth\s*<[^>]*>\s*=\s*false;", mat)
        assert re.search(r"UI_SplitPreview\s*<[^>]*>\s*=\s*true;", mat)
        assert re.search(r"UI_SplitPosition\s*<[^>]*>\s*=\s*35\.0;", mat)

    def check_preset_parity() -> None:
        p0 = CURATED_PRESETS["balanced"]["params"]
        p1 = CURATED_PRESETS["cinematic"]["params"]
        p2 = CURATED_PRESETS["competitive"]["params"]
        p3 = CURATED_PRESETS["rpg"]["params"]
        p4 = CURATED_PRESETS["night"]["params"]
        assert p0["intensity"] == 50 and not p0["enableFilmic"]
        assert p1["enableFilmic"] and p1["filmicMode"] == 1 and p1["filmicStrength"] == 30
        assert p1["enableBloom"] and p1["bloomIntensity"] == 20 and p1["bloomThreshold"] == 68
        assert p1["enableAmbientOcclusion"] and p1["ambientOcclusion"] == 30
        assert p1["enableDeband"] and p1["debandStrength"] == 35
        assert p2["intensity"] == 60 and p2["enableDarkLighting"] and p2["darkLighting"] == 35
        assert p2["enableSharpen"] and p2["sharpen"] == 45
        assert p3["intensity"] == 65 and p3["saturationBoost"] == 70 and p3["enableClarity"] and p3["clarity"] == 25
        assert p4["intensity"] == 40 and p4["saturationBoost"] == 35 and p4["enableNightMode"] and p4["nightMode"] == 35

    def check_global_vkbasalt_apply_and_backup() -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            xdg_cfg = tmp_path / "cfg"
            xdg_dat = tmp_path / "data"
            legacy_conf = xdg_cfg / "vkBasalt" / "vkBasalt.conf"
            legacy_conf.parent.mkdir(parents=True, exist_ok=True)
            legacy_conf.write_text("# Old user config\neffects = cas\n", encoding="utf-8", newline="\n")

            cfg = default_config()
            cfg["profiles"][0] = copy.deepcopy(CURATED_PRESETS["rpg"]["params"])
            cfg["toggleKey"] = "F10"
            res = apply_vkbasalt_config(cfg, profile_slot=1, xdg_config_home=xdg_cfg, xdg_data_home=xdg_dat)

            conf_txt = Path(res["conf_path"]).read_text(encoding="utf-8")
            assert "~" not in conf_txt
            assert f"effects = {VKBASALT_EFFECT_NAME}" in conf_txt
            assert f"{VKBASALT_EFFECT_NAME} = {res['shader_path']}" in conf_txt
            assert f"reshadeIncludePath = {res['include_dir']}" in conf_txt
            assert "toggleKey = F10" in conf_txt
            assert "depthCapture = off" in conf_txt
            assert res["launch_option"] == "ENABLE_VKBASALT=1 %command%"
            assert (legacy_conf.with_name("vkBasalt.conf.adv-backup")).is_file()
            assert Path(res["shader_path"]).is_file()
            assert (Path(res["include_dir"]) / "ReShade.fxh").is_file()

    def check_per_game_apply() -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            xdg_cfg = tmp_path / "cfg"
            xdg_dat = tmp_path / "data"
            cfg = default_config()
            cfg["enableSmaa"] = True
            cfg["depthCapture"] = True
            res = apply_vkbasalt_config(
                cfg, profile_slot=2, game_slug="Witcher 3: Next Gen",
                xdg_config_home=xdg_cfg, xdg_data_home=xdg_dat,
            )
            assert "witcher-3-next-gen.conf" in res["conf_path"]
            assert "VKBASALT_CONFIG_FILE=" in res["launch_option"]
            conf_txt = Path(res["conf_path"]).read_text(encoding="utf-8")
            assert f"effects = smaa:{VKBASALT_EFFECT_NAME}" in conf_txt
            assert "depthCapture = on" in conf_txt

    def check_preset_json_roundtrip() -> None:
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "test_preset.json"
            src = copy.deepcopy(CURATED_PRESETS["cinematic"]["params"])
            src["sharpen"] = 74
            src["enableSharpen"] = True
            export_preset_file(src, "Linux Cinematic Test", p)
            name, loaded = import_preset_file(p)
            assert name == "Linux Cinematic Test"
            assert loaded["sharpen"] == 74 and loaded["enableSharpen"]
            assert loaded["filmicMode"] == 1 and loaded["enableDeband"]

    def check_config_persistence_roundtrip() -> None:
        with tempfile.TemporaryDirectory() as tmp:
            xdg_cfg = Path(tmp) / "cfg"
            cfg = default_config()
            cfg["activeProfile"] = 3
            cfg["splitPreview"] = True
            cfg["splitPosition"] = 42
            cfg["profiles"][2] = copy.deepcopy(CURATED_PRESETS["competitive"]["params"])
            save_config(cfg, xdg_cfg)
            cfg2 = load_config(xdg_cfg)
            assert cfg2["activeProfile"] == 3
            assert cfg2["splitPreview"] is True and cfg2["splitPosition"] == 42
            assert cfg2["profiles"][2]["enableDarkLighting"] is True
            assert cfg2["profiles"][2]["darkLighting"] == 35

    def check_environment_probe() -> None:
        with tempfile.TemporaryDirectory() as tmp:
            env = detect_linux_environment(Path(tmp))
            assert isinstance(env["distro_name"], str) and len(env["distro_name"]) > 0
            assert "family" in env and "install_hint" in env

    checks.extend([
        ("Shader & ReShade.fxh LF + split preview", check_shader_assets),
        ("Uniform 44/44 substitution into .fx", check_uniform_substitution),
        ("5 Curated Starter Presets match Windows parity", check_preset_parity),
        ("Global vkBasalt.conf & legacy backup", check_global_vkbasalt_apply_and_backup),
        ("Per-game vkBasalt.conf + SMAA + VKBASALT_CONFIG_FILE", check_per_game_apply),
        ("Cross-platform .json preset export/import round-trip", check_preset_json_roundtrip),
        ("Linux config.json save/load persistence", check_config_persistence_roundtrip),
        ("Linux gaming distro & layer probe", check_environment_probe),
    ])

    passed = 0
    for idx, (label, fn) in enumerate(checks, start=1):
        try:
            fn()
            print(f"[PASS] Check {idx}/{len(checks)}: {label}")
            passed += 1
        except Exception as exc:
            print(f"[FAIL] Check {idx}/{len(checks)}: {label} -> {exc}")
            return 1

    print(f"ALL {passed}/{len(checks)} LINUX CONTROL HUB SELF-TESTS PASSED!")
    return 0


def launch_gui() -> int:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    PAL = {
        "bg": "#0E1015",
        "panel": "#161922",
        "card": "#1B202C",
        "border": "#272D3D",
        "accent": "#E02B3D",
        "accent_hover": "#FF4254",
        "text": "#F1F3F7",
        "muted": "#9AA3B5",
        "good": "#34D399",
        "warn": "#FBBF24",
    }

    root = tk.Tk()
    root.title(f"{APP_TITLE} v{APP_VERSION} (Linux)")
    root.geometry("920x680")
    root.minsize(780, 560)
    root.configure(bg=PAL["bg"])

    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure(".", background=PAL["bg"], foreground=PAL["text"], font=("Sans", 10))
    style.configure("TFrame", background=PAL["bg"])
    style.configure("Card.TFrame", background=PAL["card"])
    style.configure("TLabel", background=PAL["bg"], foreground=PAL["text"])
    style.configure("Card.TLabel", background=PAL["card"], foreground=PAL["text"])
    style.configure("Muted.TLabel", background=PAL["card"], foreground=PAL["muted"], font=("Sans", 9))
    style.configure("TNotebook", background=PAL["bg"], borderwidth=0)
    style.configure("TNotebook.Tab", background=PAL["panel"], foreground=PAL["muted"], padding=(14, 7))
    style.map("TNotebook.Tab", background=[("selected", PAL["card"])], foreground=[("selected", PAL["text"])])
    style.configure("Accent.TButton", background=PAL["accent"], foreground="#FFFFFF", padding=(14, 7), borderwidth=0)
    style.map("Accent.TButton", background=[("active", PAL["accent_hover"])])
    style.configure("Dark.TButton", background=PAL["panel"], foreground=PAL["text"], padding=(10, 6), borderwidth=1)
    style.map("Dark.TButton", background=[("active", "#262C3B")])
    style.configure("Dark.TCheckbutton", background=PAL["card"], foreground=PAL["text"])
    style.map("Dark.TCheckbutton", background=[("active", PAL["card"])])

    cfg = load_config()
    slot_var = tk.IntVar(value=cfg["activeProfile"])
    split_var = tk.BooleanVar(value=cfg["splitPreview"])
    split_pos_var = tk.IntVar(value=cfg["splitPosition"])
    toggle_var = tk.StringVar(value=cfg["toggleKey"])
    launch_enable_var = tk.BooleanVar(value=cfg["enableOnLaunch"])
    depth_var = tk.BooleanVar(value=cfg["depthCapture"])
    smaa_var = tk.BooleanVar(value=cfg["enableSmaa"])
    status_var = tk.StringVar(value="Ready. Pick a starter preset or customize filters, then click Apply to vkBasalt.")
    launch_opt_var = tk.StringVar(value="ENABLE_VKBASALT=1 %command%")

    filter_bool_vars: dict[str, tk.BooleanVar] = {}
    filter_int_vars: dict[str, tk.IntVar] = {}
    choice_vars: dict[str, tk.StringVar] = {}

    def sync_ui_from_slot() -> None:
        p = cfg["profiles"][slot_var.get() - 1]
        for k, var in filter_bool_vars.items():
            var.set(bool(p.get(k, False)))
        for k, var in filter_int_vars.items():
            var.set(int(p.get(k, 50)))
        choice_vars["filmicMode"].set(FILMIC_MODES[int(p.get("filmicMode", 0))])
        choice_vars["colorblindType"].set(COLORBLIND_TYPES[int(p.get("colorblindType", 0))])

    def sync_slot_from_ui() -> None:
        cfg["activeProfile"] = slot_var.get()
        cfg["splitPreview"] = bool(split_var.get())
        cfg["splitPosition"] = int(split_pos_var.get())
        cfg["toggleKey"] = toggle_var.get().strip() or "Home"
        cfg["enableOnLaunch"] = bool(launch_enable_var.get())
        cfg["depthCapture"] = bool(depth_var.get())
        cfg["enableSmaa"] = bool(smaa_var.get())
        p = cfg["profiles"][slot_var.get() - 1]
        for k, var in filter_bool_vars.items():
            p[k] = bool(var.get())
        for k, var in filter_int_vars.items():
            p[k] = int(var.get())
        p["filmicMode"] = FILMIC_MODES.index(choice_vars["filmicMode"].get())
        p["colorblindType"] = COLORBLIND_TYPES.index(choice_vars["colorblindType"].get())

    def do_apply(game_slug: str | None = None) -> None:
        try:
            sync_slot_from_ui()
            res = apply_vkbasalt_config(cfg, profile_slot=slot_var.get(), game_slug=game_slug)
            launch_opt_var.set(res["launch_option"])
            status_var.set(f"Applied Profile {slot_var.get()} -> {res['conf_path']}")
            refresh_diag()
        except Exception as exc:
            messagebox.showerror(APP_TITLE, f"Failed to apply vkBasalt config:\n{exc}")

    def apply_curated(preset_key: str) -> None:
        idx = slot_var.get() - 1
        cfg["profiles"][idx] = copy.deepcopy(CURATED_PRESETS[preset_key]["params"])
        sync_ui_from_slot()
        do_apply()

    # Header
    header = tk.Frame(root, bg=PAL["panel"], padx=16, pady=12)
    header.pack(fill="x")
    tk.Label(header, text=f"{APP_TITLE}  v{APP_VERSION}", bg=PAL["panel"], fg=PAL["text"], font=("Sans", 14, "bold")).pack(side="left")
    tk.Label(header, text="Linux vkBasalt Edition", bg=PAL["panel"], fg=PAL["accent"], font=("Sans", 10, "bold")).pack(side="left", padx=10)

    hdr_right = tk.Frame(header, bg=PAL["panel"])
    hdr_right.pack(side="right")
    ttk.Button(hdr_right, text="Apply to vkBasalt", style="Accent.TButton", command=lambda: do_apply()).pack(side="right", padx=(10, 0))
    for s in (3, 2, 1):
        tk.Radiobutton(
            hdr_right, text=f"Profile {s}", variable=slot_var, value=s,
            bg=PAL["panel"], fg=PAL["text"], selectcolor=PAL["accent"],
            activebackground=PAL["panel"], activeforeground=PAL["text"],
            indicatoron=False, padx=10, pady=5, borderwidth=0,
            command=sync_ui_from_slot,
        ).pack(side="right", padx=2)

    nb = ttk.Notebook(root)
    nb.pack(fill="both", expand=True, padx=12, pady=10)

    # Tab 1: Presets & Quick Setup
    tab_presets = ttk.Frame(nb)
    nb.add(tab_presets, text="Presets & Quick Setup")

    presets_grid = tk.Frame(tab_presets, bg=PAL["bg"])
    presets_grid.pack(fill="x", padx=6, pady=8)
    for col, (key, meta) in enumerate(CURATED_PRESETS.items()):
        r, c = divmod(col, 2)
        card = ttk.Frame(presets_grid, style="Card.TFrame", padding=12)
        card.grid(row=r, column=c, sticky="nsew", padx=6, pady=6)
        presets_grid.columnconfigure(c, weight=1)
        ttk.Label(card, text=meta["name"], style="Card.TLabel", font=("Sans", 11, "bold")).pack(anchor="w")
        ttk.Label(card, text=meta["subtitle"], style="Muted.TLabel", wraplength=380).pack(anchor="w", pady=(2, 8))
        ttk.Button(card, text=f"Apply {meta['name']}", style="Accent.TButton", command=lambda k=key: apply_curated(k)).pack(anchor="w")

    split_card = ttk.Frame(tab_presets, style="Card.TFrame", padding=12)
    split_card.pack(fill="x", padx=12, pady=6)
    ttk.Checkbutton(split_card, text="Split Preview (Before Left / After Right)", variable=split_var, style="Dark.TCheckbutton", command=lambda: do_apply()).pack(side="left")
    tk.Scale(
        split_card, from_=5, to=95, orient="horizontal", variable=split_pos_var,
        bg=PAL["card"], fg=PAL["text"], troughcolor=PAL["bg"], highlightthickness=0, length=240,
    ).pack(side="right")
    ttk.Label(split_card, text="Split %:", style="Muted.TLabel").pack(side="right", padx=(8, 4))

    io_card = ttk.Frame(tab_presets, style="Card.TFrame", padding=12)
    io_card.pack(fill="x", padx=12, pady=6)

    def do_import_json() -> None:
        fpath = filedialog.askopenfilename(title="Import Preset JSON", filetypes=[("JSON Presets", "*.json")])
        if not fpath:
            return
        try:
            pname, pdata = import_preset_file(Path(fpath))
            cfg["profiles"][slot_var.get() - 1] = pdata
            sync_ui_from_slot()
            do_apply()
            status_var.set(f"Imported '{pname}' into Profile {slot_var.get()} and applied.")
        except Exception as exc:
            messagebox.showerror(APP_TITLE, f"Import failed:\n{exc}")

    def do_export_json() -> None:
        sync_slot_from_ui()
        fpath = filedialog.asksaveasfilename(title="Export Preset JSON", defaultextension=".json", filetypes=[("JSON Presets", "*.json")])
        if not fpath:
            return
        export_preset_file(cfg["profiles"][slot_var.get() - 1], f"Linux Profile {slot_var.get()}", Path(fpath))
        status_var.set(f"Exported Profile {slot_var.get()} -> {fpath}")

    def do_reset_slot() -> None:
        cfg["profiles"][slot_var.get() - 1] = copy.deepcopy(DEFAULT_FILTER_PARAMS)
        sync_ui_from_slot()
        do_apply()

    ttk.Button(io_card, text="Import Windows/Linux .json", style="Dark.TButton", command=do_import_json).pack(side="left", padx=(0, 8))
    ttk.Button(io_card, text="Export .json", style="Dark.TButton", command=do_export_json).pack(side="left", padx=(0, 8))
    ttk.Button(io_card, text="Reset Slot", style="Dark.TButton", command=do_reset_slot).pack(side="left")

    steam_card = ttk.Frame(tab_presets, style="Card.TFrame", padding=12)
    steam_card.pack(fill="x", padx=12, pady=6)
    ttk.Label(steam_card, text="Steam / Lutris / Heroic Launch Option:", style="Card.TLabel", font=("Sans", 10, "bold")).pack(anchor="w")
    launch_row = tk.Frame(steam_card, bg=PAL["card"])
    launch_row.pack(fill="x", pady=(6, 0))
    tk.Entry(launch_row, textvariable=launch_opt_var, readonlybackground=PAL["bg"], fg=PAL["good"], state="readonly", relief="flat", font=("Monospace", 10)).pack(side="left", fill="x", expand=True, padx=(0, 8))
    ttk.Button(launch_row, text="Copy", style="Accent.TButton", command=lambda: status_var.set(f"Copied launch option ({copy_to_clipboard(launch_opt_var.get(), root) or 'buffer'})")).pack(side="right")

    # Tab 2: All 16 Filters (scrollable)
    tab_filters = ttk.Frame(nb)
    nb.add(tab_filters, text="16 Visual Filters")
    canvas = tk.Canvas(tab_filters, bg=PAL["bg"], highlightthickness=0)
    scrollbar = ttk.Scrollbar(tab_filters, orient="vertical", command=canvas.yview)
    scroll_frame = ttk.Frame(canvas)
    scroll_frame.bind("<Configure>", lambda _e: canvas.configure(scrollregion=canvas.bbox("all")))
    win_id = canvas.create_window((0, 0), window=scroll_frame, anchor="nw")
    canvas.bind("<Configure>", lambda e: canvas.itemconfigure(win_id, width=e.width))
    canvas.configure(yscrollcommand=scrollbar.set)
    canvas.pack(side="left", fill="both", expand=True)
    scrollbar.pack(side="right", fill="y")

    choice_vars["filmicMode"] = tk.StringVar(value=FILMIC_MODES[0])
    choice_vars["colorblindType"] = tk.StringVar(value=COLORBLIND_TYPES[0])

    for grp in FILTER_UI_GROUPS:
        card = ttk.Frame(scroll_frame, style="Card.TFrame", padding=10)
        card.pack(fill="x", padx=10, pady=5)
        bvar = tk.BooleanVar(value=False)
        filter_bool_vars[grp["enable_key"]] = bvar
        ttk.Checkbutton(card, text=grp["title"], variable=bvar, style="Dark.TCheckbutton").pack(anchor="w")
        if "choice" in grp:
            clabel, ckey, citems = grp["choice"]
            crow = tk.Frame(card, bg=PAL["card"])
            crow.pack(fill="x", pady=4)
            ttk.Label(crow, text=f"{clabel}:", style="Muted.TLabel").pack(side="left", padx=(20, 8))
            ttk.Combobox(crow, textvariable=choice_vars[ckey], values=citems, state="readonly", width=26).pack(side="left")
        for slabel, skey, slo, shi in grp["sliders"]:
            ivar = tk.IntVar(value=50)
            filter_int_vars[skey] = ivar
            srow = tk.Frame(card, bg=PAL["card"])
            srow.pack(fill="x", pady=2)
            ttk.Label(srow, text=slabel, style="Muted.TLabel", width=28).pack(side="left", padx=(20, 8))
            tk.Scale(
                srow, from_=slo, to=shi, orient="horizontal", variable=ivar,
                bg=PAL["card"], fg=PAL["text"], troughcolor=PAL["bg"], highlightthickness=0,
            ).pack(side="left", fill="x", expand=True)

    # Tab 3: Linux Setup & Per-Game
    tab_linux = ttk.Frame(nb)
    nb.add(tab_linux, text="Linux Setup & Per-Game")
    diag_card = ttk.Frame(tab_linux, style="Card.TFrame", padding=12)
    diag_card.pack(fill="x", padx=12, pady=8)
    ttk.Label(diag_card, text="Linux Gaming Environment Diagnostics", style="Card.TLabel", font=("Sans", 11, "bold")).pack(anchor="w")
    diag_text = tk.StringVar(value="")
    ttk.Label(diag_card, textvariable=diag_text, style="Muted.TLabel", justify="left").pack(anchor="w", pady=(6, 8))

    def refresh_diag() -> None:
        env = detect_linux_environment()
        diag_text.set(
            f"Distro: {env['distro_name']} (family={env['family']}, immutable={env['immutable']})\n"
            f"Native vkBasalt Layer: {env['native_layer_json'] or 'Not detected'} | 64-bit: {env['native_64_lib'] or 'Missing'} | 32-bit: {env['native_32_lib'] or 'Missing'}\n"
            f"Flatpak vkBasalt Extension: {'Installed' if env['flatpak_vkbasalt'] else 'Not found'} (Flatpak CLI: {env['flatpak_installed']})\n"
            f"Active vkBasalt.conf: {env['active_conf'] or 'None'} (Managed: {env['active_conf_managed']})\n"
            f"Tip: {env['install_hint']}"
        )

    btn_row = tk.Frame(diag_card, bg=PAL["card"])
    btn_row.pack(fill="x")
    ttk.Button(btn_row, text="Refresh Diagnostics", style="Dark.TButton", command=refresh_diag).pack(side="left", padx=(0, 8))
    ttk.Button(
        btn_row,
        text="Fix Flatpak Permissions",
        style="Dark.TButton",
        command=lambda: messagebox.showinfo(APP_TITLE, apply_flatpak_overrides()[1]),
    ).pack(side="left")

    vk_opts = ttk.Frame(tab_linux, style="Card.TFrame", padding=12)
    vk_opts.pack(fill="x", padx=12, pady=6)
    ttk.Label(vk_opts, text="vkBasalt Runtime Options", style="Card.TLabel", font=("Sans", 11, "bold")).pack(anchor="w", pady=(0, 6))
    row_tk = tk.Frame(vk_opts, bg=PAL["card"])
    row_tk.pack(fill="x", pady=3)
    ttk.Label(row_tk, text="In-Game Toggle Key:", style="Muted.TLabel").pack(side="left", padx=(0, 8))
    ttk.Combobox(row_tk, textvariable=toggle_var, values=TOGGLE_KEYS, state="readonly", width=16).pack(side="left", padx=(0, 16))
    ttk.Checkbutton(row_tk, text="Enable on Launch", variable=launch_enable_var, style="Dark.TCheckbutton").pack(side="left", padx=(0, 16))
    ttk.Checkbutton(row_tk, text="Include SMAA", variable=smaa_var, style="Dark.TCheckbutton").pack(side="left", padx=(0, 16))
    ttk.Checkbutton(row_tk, text="Experimental Depth Capture", variable=depth_var, style="Dark.TCheckbutton").pack(side="left")

    game_card = ttk.Frame(tab_linux, style="Card.TFrame", padding=12)
    game_card.pack(fill="x", padx=12, pady=6)
    ttk.Label(game_card, text="Per-Game vkBasalt Config Generator", style="Card.TLabel", font=("Sans", 11, "bold")).pack(anchor="w")
    ttk.Label(game_card, text="Creates a dedicated vkBasalt .conf for a specific game without overwriting your global default.", style="Muted.TLabel").pack(anchor="w", pady=(2, 6))
    grow = tk.Frame(game_card, bg=PAL["card"])
    grow.pack(fill="x")
    game_name_var = tk.StringVar(value="witcher3")
    tk.Entry(grow, textvariable=game_name_var, bg=PAL["bg"], fg=PAL["text"], insertbackground=PAL["text"], relief="flat").pack(side="left", fill="x", expand=True, padx=(0, 8))
    ttk.Button(grow, text="Generate Per-Game Config", style="Accent.TButton", command=lambda: do_apply(game_slug=game_name_var.get())).pack(side="right")

    # Tab 4: About
    tab_about = ttk.Frame(nb)
    nb.add(tab_about, text="About")
    about_card = ttk.Frame(tab_about, style="Card.TFrame", padding=16)
    about_card.pack(fill="both", expand=True, padx=12, pady=12)
    about_msg = (
        f"{APP_TITLE} v{APP_VERSION} (Linux vkBasalt Control Hub)\n"
        "Author: arthurj119  |  License: MIT\n"
        "Repository: https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance\n\n"
        "Official Linux Gaming Distro Targets:\n"
        "  • Arch Linux / CachyOS / EndeavourOS\n"
        "  • SteamOS / Steam Deck (user-space + Flatpak VulkanLayer)\n"
        "  • Fedora / Nobara / Bazzite\n"
        "  • Ubuntu / Pop!_OS / Linux Mint\n\n"
        "Linux Pipeline Notes:\n"
        "  • All 16 calibrated ReShade .fx filters, 5 curated presets, Split Preview, and Windows-compatible .json\n"
        "    presets run natively through vkBasalt (Wayland, X11, Gamescope, Proton/DXVK/VKD3D-Proton, Vulkan).\n"
        "  • For spatial upscaling on Linux, combine with Gamescope FSR (-U / --filter fsr) in Steam launch options.\n"
        "  • DirectX 11 Optical Flow Frame Gen and Windows True HDR10 scRGB remain Windows-only features."
    )
    ttk.Label(about_card, text=about_msg, style="Card.TLabel", justify="left").pack(anchor="w")

    footer = tk.Frame(root, bg=PAL["panel"], padx=16, pady=8)
    footer.pack(fill="x")
    tk.Label(footer, textvariable=status_var, bg=PAL["panel"], fg=PAL["muted"], font=("Sans", 9)).pack(side="left")

    sync_ui_from_slot()
    refresh_diag()
    root.mainloop()
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=APP_SLUG,
        description="AI Dynamic Vibrance Enhance (v2.0.0) - Linux Control Hub & vkBasalt Configurator",
    )
    parser.add_argument("--version", action="store_true", help="Print version and exit")
    parser.add_argument("--self-test", action="store_true", help="Run automated offline validation suite")

    sub = parser.add_subparsers(dest="command")
    sub.add_parser("gui", help="Launch dark graphical Control Hub (default)")
    sub.add_parser("status", help="Inspect distro, vkBasalt layer, Flatpak, and active config")
    sub.add_parser("presets", help="List the 5 curated starter presets")
    sub.add_parser("fix-flatpak", help="Grant Flatpak apps access to vkBasalt config and data folders")

    p_apply = sub.add_parser("apply", help="Apply preset or profile slot to vkBasalt")
    p_apply.add_argument("--preset", choices=list(CURATED_PRESETS.keys()), help="Curated starter preset")
    p_apply.add_argument("--profile", type=int, choices=[1, 2, 3], help="Profile slot (1..3)")
    p_apply.add_argument("--split", type=int, metavar="PCT", help="Enable Split Preview at PCT (5..95)")
    p_apply.add_argument("--no-split", action="store_true", help="Disable Split Preview")
    p_apply.add_argument("--toggle-key", help="In-game vkBasalt toggle key (default Home)")
    p_apply.add_argument("--depth-capture", action="store_true", help="Enable experimental vkBasalt depthCapture")
    p_apply.add_argument("--smaa", action="store_true", help="Chain vkBasalt built-in SMAA before vibrance")
    p_apply.add_argument("--game", help="Create a per-game config slug instead of global vkBasalt.conf")
    p_apply.add_argument("--output-conf", help="Explicit vkBasalt.conf output path")

    p_imp = sub.add_parser("import-preset", help="Import Windows/Linux .json preset into slot and apply")
    p_imp.add_argument("json_file", help="Path to .json preset file")
    p_imp.add_argument("--profile", type=int, choices=[1, 2, 3], default=1)
    p_imp.add_argument("--no-apply", action="store_true")

    p_exp = sub.add_parser("export-preset", help="Export active slot to Windows/Linux .json preset")
    p_exp.add_argument("json_file", help="Output .json preset file path")
    p_exp.add_argument("--profile", type=int, choices=[1, 2, 3], default=1)
    p_exp.add_argument("--name", help="Preset display name")

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    if args.version:
        print(f"{APP_TITLE} {APP_VERSION} (Linux)")
        return 0
    if args.self_test:
        return run_self_tests()

    cmd = args.command or "gui"
    if cmd == "gui":
        return launch_gui()
    if cmd == "status":
        print(json.dumps(detect_linux_environment(), indent=2))
        return 0
    if cmd == "presets":
        for k, meta in CURATED_PRESETS.items():
            print(f"{k:12s} - {meta['name']}: {meta['subtitle']}")
        return 0
    if cmd == "fix-flatpak":
        ok, msg = apply_flatpak_overrides()
        print(msg)
        return 0 if ok else 1

    cfg = load_config()
    if cmd == "apply":
        slot = args.profile or cfg["activeProfile"]
        cfg["activeProfile"] = slot
        if args.preset:
            cfg["profiles"][slot - 1] = copy.deepcopy(CURATED_PRESETS[args.preset]["params"])
        if args.split is not None:
            cfg["splitPreview"] = True
            cfg["splitPosition"] = _clamp_int(args.split, 5, 95, 50)
        elif args.no_split:
            cfg["splitPreview"] = False
        if args.toggle_key:
            cfg["toggleKey"] = args.toggle_key.strip()
        if args.depth_capture:
            cfg["depthCapture"] = True
        if args.smaa:
            cfg["enableSmaa"] = True
        out_override = Path(args.output_conf) if args.output_conf else None
        res = apply_vkbasalt_config(
            cfg, profile_slot=slot, game_slug=args.game, output_conf_override=out_override
        )
        print(json.dumps(res, indent=2))
        return 0

    if cmd == "import-preset":
        name, params = import_preset_file(Path(args.json_file))
        cfg["profiles"][args.profile - 1] = params
        cfg["activeProfile"] = args.profile
        save_config(cfg)
        if not args.no_apply:
            res = apply_vkbasalt_config(cfg, profile_slot=args.profile)
            print(f"Imported '{name}' -> Profile {args.profile} & applied: {res['conf_path']}")
        else:
            print(f"Imported '{name}' -> Profile {args.profile}")
        return 0

    if cmd == "export-preset":
        out_path = export_preset_file(
            cfg["profiles"][args.profile - 1],
            args.name or f"Linux Profile {args.profile}",
            Path(args.json_file),
        )
        print(f"Exported Profile {args.profile} -> {out_path}")
        return 0

    parser.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
