#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_DIR="${REPO_ROOT}/linux/dist"
PKG_NAME="ai-dynamic-vibrance-enhance"
VERSION="2.0.0"

STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "${STAGE_DIR}"' EXIT

mkdir -p \
  "${STAGE_DIR}/DEBIAN" \
  "${STAGE_DIR}/usr/bin" \
  "${STAGE_DIR}/usr/share/${PKG_NAME}/shaders" \
  "${STAGE_DIR}/usr/share/applications" \
  "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps" \
  "${STAGE_DIR}/usr/share/doc/${PKG_NAME}" \
  "${OUT_DIR}"

install -m 0755 "${REPO_ROOT}/linux/adv_control_hub.py" "${STAGE_DIR}/usr/bin/${PKG_NAME}"
install -m 0644 "${REPO_ROOT}/shaders/AIDynamicVibranceEnhance.fx" "${STAGE_DIR}/usr/share/${PKG_NAME}/shaders/AIDynamicVibranceEnhance.fx"
install -m 0644 "${REPO_ROOT}/linux/shaders/ReShade.fxh" "${STAGE_DIR}/usr/share/${PKG_NAME}/shaders/ReShade.fxh"
install -m 0644 "${REPO_ROOT}/linux/assets/${PKG_NAME}.desktop" "${STAGE_DIR}/usr/share/applications/${PKG_NAME}.desktop"
install -m 0644 "${REPO_ROOT}/linux/assets/${PKG_NAME}.svg" "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps/${PKG_NAME}.svg"
install -m 0644 "${REPO_ROOT}/LICENSE" "${STAGE_DIR}/usr/share/doc/${PKG_NAME}/copyright"

cp "${SCRIPT_DIR}/control" "${STAGE_DIR}/DEBIAN/control"
INSTALLED_KB="$(du -sk "${STAGE_DIR}/usr" | awk '{print $1}')"
echo "Installed-Size: ${INSTALLED_KB}" >> "${STAGE_DIR}/DEBIAN/control"

cat > "${STAGE_DIR}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "${STAGE_DIR}/DEBIAN/postinst"

(
  cd "${STAGE_DIR}"
  find usr -type f | sort | xargs md5sum > "${STAGE_DIR}/DEBIAN/md5sums"
)
chmod 0644 "${STAGE_DIR}/DEBIAN/md5sums"

OUT_DEB="${OUT_DIR}/${PKG_NAME}_${VERSION}_all.deb"
dpkg-deb --build --root-owner-group "${STAGE_DIR}" "${OUT_DEB}"
echo "Built Debian/Ubuntu package: ${OUT_DEB}"
