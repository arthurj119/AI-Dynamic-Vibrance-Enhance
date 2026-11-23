#!/usr/bin/env bash
set -euo pipefail

APP_NAME="ai-dynamic-vibrance-enhance"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INSTALL_SCOPE="auto"   # auto | user | system
SKIP_DEPS=0
INIT_PRESET=""

usage() {
  cat <<EOF
AI Dynamic Vibrance Enhance (v2.0.0) - Linux Gaming Installer
Supported gaming distros:
  - Arch Linux / CachyOS / EndeavourOS
  - SteamOS (Steam Deck)
  - Fedora / Nobara / Bazzite
  - Ubuntu / Pop!_OS / Linux Mint

Usage: $0 [OPTIONS]
  --user                  Install to ~/.local (default; always used on SteamOS / Bazzite)
  --system                Install system-wide to /usr/local (requires sudo)
  --skip-deps             Skip installing vkbasalt / python3-tk packages
  --preset <name>         Apply initial starter preset (balanced|cinematic|competitive|rpg|night)
  -h, --help              Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user) INSTALL_SCOPE="user"; shift ;;
    --system) INSTALL_SCOPE="system"; shift ;;
    --skip-deps) SKIP_DEPS=1; shift ;;
    --preset)
      INIT_PRESET="${2:-}"
      if [[ -z "${INIT_PRESET}" ]]; then
        echo "Error: --preset requires a value." >&2
        exit 1
      fi
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

locate_file() {
  for cand in "$@"; do
    if [[ -f "${cand}" ]]; then
      printf '%s\n' "${cand}"
      return 0
    fi
  done
  return 1
}

HUB_SRC="$(locate_file "${SCRIPT_DIR}/adv_control_hub.py")"
FX_SRC="$(locate_file "${SCRIPT_DIR}/shaders/AIDynamicVibranceEnhance.fx" "${SCRIPT_DIR}/../shaders/AIDynamicVibranceEnhance.fx")"
FXH_SRC="$(locate_file "${SCRIPT_DIR}/shaders/ReShade.fxh")"
ICON_SRC="$(locate_file "${SCRIPT_DIR}/assets/${APP_NAME}.svg")"
DESKTOP_SRC="$(locate_file "${SCRIPT_DIR}/assets/${APP_NAME}.desktop")"

DISTRO_ID="unknown"
DISTRO_LIKE=""
PRETTY_NAME="Linux"
VARIANT_ID=""
if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  DISTRO_ID="${ID:-unknown}"
  DISTRO_LIKE="${ID_LIKE:-}"
  PRETTY_NAME="${PRETTY_NAME:-$DISTRO_ID}"
  VARIANT_ID="${VARIANT_ID:-}"
fi

IS_IMMUTABLE=0
if [[ -f /run/ostree-booted ]] || [[ "${DISTRO_ID}" == "steamos" ]] || [[ "${DISTRO_ID}" == "bazzite" ]]; then
  IS_IMMUTABLE=1
fi

if [[ "${INSTALL_SCOPE}" == "auto" ]]; then
  if [[ ${EUID} -eq 0 && ${IS_IMMUTABLE} -eq 0 ]]; then
    INSTALL_SCOPE="system"
  else
    INSTALL_SCOPE="user"
  fi
fi

if [[ ${IS_IMMUTABLE} -eq 1 && "${INSTALL_SCOPE}" == "system" ]]; then
  echo "Detected immutable OS (${PRETTY_NAME}); switching to user-space install (~/.local)."
  INSTALL_SCOPE="user"
fi

echo "==> Detected OS: ${PRETTY_NAME} (${DISTRO_ID}) | Install mode: ${INSTALL_SCOPE}"

configure_flatpak_vkbasalt() {
  if command -v flatpak >/dev/null 2>&1; then
    echo "==> Configuring Flatpak vkBasalt sandbox permissions..."
    flatpak --user override --filesystem=xdg-config/vkBasalt:ro --filesystem=xdg-data/vkBasalt:ro || true
    if ! flatpak list --runtime --columns=application 2>/dev/null | grep -q "org.freedesktop.Platform.VulkanLayer.vkBasalt"; then
      echo "==> Installing Flatpak vkBasalt VulkanLayer extension (if flathub is configured)..."
      flatpak install -y --noninteractive flathub org.freedesktop.Platform.VulkanLayer.vkBasalt || true
    fi
  fi
}

install_distro_deps() {
  local id_low="${DISTRO_ID,,}"
  local like_low="${DISTRO_LIKE,,}"

  if [[ "${id_low}" == "steamos" ]] || [[ "${id_low}" == "bazzite" ]] || [[ -f /run/ostree-booted ]]; then
    echo "==> Immutable gaming OS detected (${PRETTY_NAME}); skipping root package installs."
    configure_flatpak_vkbasalt
    return 0
  fi

  if [[ "${id_low}" =~ ^(arch|cachyos|endeavouros|manjaro|garuda)$ ]] || [[ "${like_low}" == *arch* ]]; then
    echo "==> Installing dependencies for Arch / CachyOS / EndeavourOS..."
    sudo pacman -S --needed --noconfirm python tk
    if pacman -Si vkbasalt >/dev/null 2>&1; then
      sudo pacman -S --needed --noconfirm vkbasalt
      if pacman -Si lib32-vkbasalt >/dev/null 2>&1; then
        sudo pacman -S --needed --noconfirm lib32-vkbasalt
      fi
    elif [[ ${EUID} -ne 0 ]] && command -v paru >/dev/null 2>&1; then
      paru -S --needed --noconfirm vkbasalt lib32-vkbasalt || true
    elif [[ ${EUID} -ne 0 ]] && command -v yay >/dev/null 2>&1; then
      yay -S --needed --noconfirm vkbasalt lib32-vkbasalt || true
    else
      echo "Note: vkbasalt is in the AUR on standard Arch. Install with: yay -S vkbasalt lib32-vkbasalt"
    fi
    configure_flatpak_vkbasalt
  elif [[ "${id_low}" =~ ^(fedora|nobara)$ ]] || [[ "${like_low}" == *fedora* ]]; then
    echo "==> Installing dependencies for Fedora / Nobara..."
    sudo dnf install -y vkBasalt python3 python3-tkinter
    sudo dnf install -y vkBasalt.i686 >/dev/null 2>&1 || true
    configure_flatpak_vkbasalt
  elif [[ "${id_low}" =~ ^(ubuntu|pop|linuxmint|neon)$ ]] || [[ "${like_low}" == *ubuntu* ]] || [[ "${like_low}" == *debian* ]]; then
    echo "==> Installing dependencies for Ubuntu / Pop!_OS / Linux Mint..."
    sudo apt-get update
    sudo apt-get install -y vkbasalt python3 python3-tk
    configure_flatpak_vkbasalt
  else
    echo "Note: Official packaging targets Arch/CachyOS/EndeavourOS, SteamOS, Fedora/Nobara/Bazzite, and Ubuntu/Pop!_OS/Mint."
    echo "Installing portable user-space files only; make sure 'vkbasalt' and 'python3-tk' are installed."
    configure_flatpak_vkbasalt
  fi
}

if [[ ${SKIP_DEPS} -eq 0 ]]; then
  install_distro_deps
else
  echo "==> Skipping dependency installation (--skip-deps)."
fi

if [[ "${INSTALL_SCOPE}" == "system" ]]; then
  BIN_DIR="/usr/local/bin"
  SHARE_DIR="/usr/local/share/${APP_NAME}"
  DESKTOP_DIR="/usr/local/share/applications"
  ICON_DIR="/usr/local/share/icons/hicolor/scalable/apps"
  SUDO="sudo"
  [[ ${EUID} -eq 0 ]] && SUDO=""
else
  BIN_DIR="${HOME}/.local/bin"
  SHARE_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/${APP_NAME}"
  DESKTOP_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
  ICON_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/icons/hicolor/scalable/apps"
  SUDO=""
fi

echo "==> Installing files into ${SHARE_DIR} and ${BIN_DIR}..."
${SUDO} install -d "${BIN_DIR}" "${SHARE_DIR}/shaders" "${DESKTOP_DIR}" "${ICON_DIR}"
${SUDO} install -m 0755 "${HUB_SRC}" "${BIN_DIR}/${APP_NAME}"
${SUDO} install -m 0644 "${FX_SRC}" "${SHARE_DIR}/shaders/AIDynamicVibranceEnhance.fx"
${SUDO} install -m 0644 "${FXH_SRC}" "${SHARE_DIR}/shaders/ReShade.fxh"
${SUDO} install -m 0644 "${ICON_SRC}" "${ICON_DIR}/${APP_NAME}.svg"
${SUDO} install -m 0644 "${DESKTOP_SRC}" "${DESKTOP_DIR}/${APP_NAME}.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
  ${SUDO} update-desktop-database "${DESKTOP_DIR}" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  ${SUDO} gtk-update-icon-cache -f -t "$(dirname "$(dirname "${ICON_DIR}")")" >/dev/null 2>&1 || true
fi

TARGET_BIN="${BIN_DIR}/${APP_NAME}"
echo "==> Running offline self-test..."
"${TARGET_BIN}" --self-test

VKB_CONF="${XDG_CONFIG_HOME:-${HOME}/.config}/vkBasalt/vkBasalt.conf"
if [[ -n "${INIT_PRESET}" ]]; then
  echo "==> Applying starter preset '${INIT_PRESET}'..."
  "${TARGET_BIN}" apply --preset "${INIT_PRESET}"
elif [[ ! -f "${VKB_CONF}" ]]; then
  echo "==> Applying default 'balanced' starter preset..."
  "${TARGET_BIN}" apply --preset balanced
fi

echo ""
echo "===================================================================="
echo "  AI Dynamic Vibrance Enhance (v2.0.0) installed successfully!"
echo "===================================================================="
echo "  Control Hub GUI : ${TARGET_BIN}"
echo "  vkBasalt config : ${VKB_CONF}"
echo "  Steam Launch Opt: ENABLE_VKBASALT=1 %command%"
echo "  In-Game Toggle  : Home key (customizable in Control Hub)"
echo ""
if [[ "${INSTALL_SCOPE}" == "user" ]] && [[ ":${PATH}:" != *":${BIN_DIR}:"* ]]; then
  echo "  Tip: Add ${BIN_DIR} to PATH in ~/.bashrc or ~/.zshrc if 'command not found'."
fi
