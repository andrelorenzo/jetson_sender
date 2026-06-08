#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SERVICE_NAME="${SERVICE_NAME:-rs_sender}"
RUN_USER="${RUN_USER:-vision}"
RUN_GROUP="${RUN_GROUP:-vision}"
EXECUTABLE_PATH="${EXECUTABLE_PATH:-${REPO_ROOT}/build/rs_sender}"
CONFIG_PATH="${CONFIG_PATH:-${REPO_ROOT}/params/config.txt}"
SERVICE_PATH="/etc/systemd/system/${SERVICE_NAME}.service"

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

if [[ ! -x "${EXECUTABLE_PATH}" ]]; then
    echo "No existe el ejecutable o no es ejecutable: ${EXECUTABLE_PATH}"
    echo "Compila primero el proyecto en la Jetson."
    exit 1
fi

if [[ ! -f "${CONFIG_PATH}" ]]; then
    echo "No existe el archivo de configuracion: ${CONFIG_PATH}"
    exit 1
fi

TMP_SERVICE="$(mktemp)"
cat > "${TMP_SERVICE}" <<EOF
[Unit]
Description=Jetson RealSense Pasarela
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${RUN_USER}
Group=${RUN_GROUP}
WorkingDirectory=${REPO_ROOT}
ExecStart=${EXECUTABLE_PATH} -file ${CONFIG_PATH}
Restart=always
RestartSec=2
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

echo "Instalando servicio en ${SERVICE_PATH}"
${SUDO} cp "${TMP_SERVICE}" "${SERVICE_PATH}"
rm -f "${TMP_SERVICE}"

${SUDO} systemctl daemon-reload
${SUDO} systemctl enable "${SERVICE_NAME}"
${SUDO} systemctl restart "${SERVICE_NAME}"

echo "Servicio instalado y reiniciado."
echo "Comandos utiles:"
echo "  sudo systemctl status ${SERVICE_NAME}"
echo "  sudo journalctl -u ${SERVICE_NAME} -f"
