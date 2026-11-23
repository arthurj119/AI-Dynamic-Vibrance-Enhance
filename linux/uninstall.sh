#!/usr/bin/env bash
set -euo pipefail

APP_NAME="ai-dynamic-vibrance-enhance"

echo "==> Removing AI Dynamic Vibrance Enhance (Linux)..."

remove_prefix() {
  local bin_dir="$1"
  local share_base="$2"
  local use_sudo="$3"
  local cmd_prefix=""
  [[ "${use_sudo}" == "1" && ${EUID} -ne 0 ]] && cmd_prefix="sudo"

  local removed=0
  for target in \
    "${bin_dir}/${APP_NAME}" \
    "${share_base}/applications/${APP_NAME}.desktop" \
    "${share_base}/icons/hicolor/scalable/apps/${APP_NAME}.svg"
  do
    if [[ -e "${target}" ]]; then
      ${cmd_prefix} rm -f "${target}"
      echo "  Removed ${target}"
      removed=1
    fi
  done

  if [[ -d "${share_base}/${APP_NAME}" ]]; then
    ${cmd_prefix} rm -rf "${share_base}/${APP_NAME}"
    echo "  Removed ${share_base}/${APP_NAME}"
    removed=1
  fi
  return 0
}

remove_prefix "${HOME}/.local/bin" "${XDG_DATA_HOME:-${HOME}/.local/share}" "0"

if [[ -e "/usr/local/bin/${APP_NAME}" || -d "/usr/local/share/${APP_NAME}" ]]; then
  remove_prefix "/usr/local/bin" "/usr/local/share" "1"
fi

USER_VKB_DATA="${XDG_DATA_HOME:-${HOME}/.local/share}/vkBasalt/ai-dynamic-vibrance"
if [[ -d "${USER_VKB_DATA}" ]]; then
  rm -rf "${USER_VKB_DATA}"
  echo "  Removed generated vkBasalt shader cache: ${USER_VKB_DATA}"
fi

USER_VKB_CONF="${XDG_CONFIG_HOME:-${HOME}/.config}/vkBasalt/vkBasalt.conf"
if [[ -f "${USER_VKB_CONF}" ]] && head -n 1 "${USER_VKB_CONF}" | grep -q "Managed by AI Dynamic Vibrance Enhance"; then
  if [[ -f "${USER_VKB_CONF}.adv-backup" ]]; then
    mv -f "${USER_VKB_CONF}.adv-backup" "${USER_VKB_CONF}"
    echo "  Restored previous backup to ${USER_VKB_CONF}"
  else
    rm -f "${USER_VKB_CONF}"
    echo "  Removed managed ${USER_VKB_CONF}"
  fi
fi

echo "Done. Saved presets/config remain in ${XDG_CONFIG_HOME:-${HOME}/.config}/${APP_NAME} (delete manually if desired)."
