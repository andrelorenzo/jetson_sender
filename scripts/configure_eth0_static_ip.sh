#!/usr/bin/env bash
set -euo pipefail

INTERFACE="${INTERFACE:-eth0}"
ADDRESS="${ADDRESS:-192.168.0.80/24}"
GATEWAY="${GATEWAY:-}"
DNS="${DNS:-}"
NETPLAN_FILE="/etc/netplan/99-${INTERFACE}-static.yaml"

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

if command -v nmcli >/dev/null 2>&1; then
    echo "Configurando ${INTERFACE} con NetworkManager"

    CONNECTION_NAME="$(${SUDO} nmcli -t -f NAME,DEVICE connection show | awk -F: -v dev="${INTERFACE}" '$2 == dev {print $1; exit}')"
    if [[ -z "${CONNECTION_NAME}" ]]; then
        CONNECTION_NAME="${INTERFACE}-static"
        ${SUDO} nmcli connection add type ethernet ifname "${INTERFACE}" con-name "${CONNECTION_NAME}"
    fi

    ${SUDO} nmcli connection modify "${CONNECTION_NAME}" ipv4.method manual ipv4.addresses "${ADDRESS}"

    if [[ -n "${GATEWAY}" ]]; then
        ${SUDO} nmcli connection modify "${CONNECTION_NAME}" ipv4.gateway "${GATEWAY}"
    else
        ${SUDO} nmcli connection modify "${CONNECTION_NAME}" -ipv4.gateway
    fi

    if [[ -n "${DNS}" ]]; then
        ${SUDO} nmcli connection modify "${CONNECTION_NAME}" ipv4.dns "${DNS}"
    else
        ${SUDO} nmcli connection modify "${CONNECTION_NAME}" ipv4.ignore-auto-dns yes
        ${SUDO} nmcli connection modify "${CONNECTION_NAME}" -ipv4.dns
    fi

    ${SUDO} nmcli connection modify "${CONNECTION_NAME}" connection.autoconnect yes
    ${SUDO} nmcli connection up "${CONNECTION_NAME}"
else
    echo "NetworkManager no disponible; creando configuracion netplan"

    TMP_FILE="$(mktemp)"
    cat > "${TMP_FILE}" <<EOF
network:
  version: 2
  renderer: networkd
  ethernets:
    ${INTERFACE}:
      dhcp4: false
      addresses:
        - ${ADDRESS}
EOF

    if [[ -n "${GATEWAY}" ]]; then
        cat >> "${TMP_FILE}" <<EOF
      gateway4: ${GATEWAY}
EOF
    fi

    if [[ -n "${DNS}" ]]; then
        cat >> "${TMP_FILE}" <<EOF
      nameservers:
        addresses: [${DNS}]
EOF
    fi

    ${SUDO} cp "${TMP_FILE}" "${NETPLAN_FILE}"
    rm -f "${TMP_FILE}"
    ${SUDO} netplan apply
fi

echo "IP estatica configurada para ${INTERFACE} en ${ADDRESS}"
