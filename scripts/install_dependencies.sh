#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MAVLINK_DIR="${REPO_ROOT}/third_party/mavlink"
MAVLINK_REPO_URL="${MAVLINK_REPO_URL:-https://github.com/mavlink/c_library_v2.git}"
INTEL_RS_LIST="/etc/apt/sources.list.d/librealsense.list"
INTEL_RS_KEYRING="/usr/share/keyrings/librealsense-archive-keyring.gpg"

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

echo "[1/5] Instalando paquetes base"
${SUDO} apt-get update
${SUDO} apt-get install -y \
    apt-transport-https \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    git \
    gnupg2 \
    lsb-release \
    pkg-config \
    software-properties-common

echo "[2/5] Instalando GStreamer y RTSP server"
${SUDO} apt-get install -y \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-tools \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer1.0-dev \
    libgstrtspserver-1.0-dev

echo "[3/5] Instalando librealsense2"
UBUNTU_CODENAME="$(lsb_release -cs)"
if ! apt-cache show librealsense2-dev >/dev/null 2>&1; then
    echo "Repositorio librealsense2 no encontrado; anadiendo repositorio oficial"
    curl -fsSL "https://librealsense.intel.com/Debian/apt-repo/public.gpg" | ${SUDO} gpg --dearmor -o "${INTEL_RS_KEYRING}"
    echo "deb [signed-by=${INTEL_RS_KEYRING}] https://librealsense.intel.com/Debian/apt-repo ${UBUNTU_CODENAME} main" | ${SUDO} tee "${INTEL_RS_LIST}" >/dev/null
    ${SUDO} apt-get update
fi

${SUDO} apt-get install -y librealsense2-dev librealsense2-utils || {
    echo "No se pudo instalar librealsense2-dev automaticamente."
    echo "Revisa la compatibilidad de librealsense2 con tu JetPack/L4T antes de continuar."
    exit 1
}

echo "[4/5] Descargando cabeceras MAVLink en third_party/mavlink"
mkdir -p "${REPO_ROOT}/third_party"

if [[ -d "${MAVLINK_DIR}/mavlink/.git" ]]; then
    git -C "${MAVLINK_DIR}/mavlink" pull --ff-only
elif [[ -d "${MAVLINK_DIR}/mavlink" ]]; then
    echo "La carpeta ${MAVLINK_DIR}/mavlink ya existe pero no es un repositorio git."
    echo "Borrala manualmente si quieres que el script descargue MAVLink."
    exit 1
else
    git clone --depth 1 "${MAVLINK_REPO_URL}" "${MAVLINK_DIR}/mavlink"
fi

echo "[5/5] Dependencias instaladas"
echo "Siguiente paso:"
echo "  cmake -S \"${REPO_ROOT}\" -B \"${REPO_ROOT}/build\" -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build \"${REPO_ROOT}/build\" -j\"\$(nproc)\""
