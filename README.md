# Jetson Pasarela

Pasarela para Jetson que captura datos de una Intel RealSense D435i y los publica hacia un cliente remoto.

En modo normal:
- Espera a que haya una D435i conectada.
- Publica video por RTSP en H264.
- Envía acelerómetro y giróscopo por UDP cuando el cliente envía `MSGID_ESTABLISH_CONNECTION`.
- Recibe comandos `twist` por UDP y los reenvía a la OrangeCube por UART usando MAVLink.
- Si `RECORD_BAG=true`, guarda los flujos de RealSense en `BAG_FILE`.

En modo debug:
- No usa la cámara física.
- Reproduce `BAG_FILE` como fuente RealSense.

## Estructura

- `src/main.cpp`: arranque del proceso.
- `src/realsense_capture.cpp`: captura y reconexión de RealSense.
- `src/video_streamer.cpp`: servidor RTSP y timestamp embebido en H264.
- `src/client_transport.cpp`: sesión cliente e IMU por UDP.
- `src/orange_cube_controller.cpp`: control OrangeCube por UDP + UART/MAVLink.
- `params/config.txt`: configuración principal.
- `scripts/`: instalación y despliegue en Jetson.

## Configuración

Archivo: [params/config.txt](/C:/Users/ajlorenzo/Documents/AAS_projects/Jetson_pasarela/params/config.txt)

Parámetros principales:
- `DEBUG=false`: usa cámara física. Si es `true`, reproduce `BAG_FILE`.
- `BAG_FILE="../bags/realsense_session.bag"`: bag de reproducción o de grabación.
- `WIDTH`, `HEIGHT`, `FPS`: resolución/frecuencia de color y depth.
- `RECORD_BAG=false`: solo se usa en modo no debug.
- `CONTROL_ENABLED=true`: habilita control OrangeCube.
- `FCU_DEV="/dev/ttyACM0"` y `FCU_BAUD=115200`: puerto serie de la OrangeCube.
- `CTRL_TIMEOUT_MS=250`: watchdog para el último `twist`.
- `MAV_TARGET_SYSTEM`, `MAV_TARGET_COMPONENT`: destino MAVLink.
- `CTRL_ROS_CONVENTION=true`: interpreta `twist` con convención ROS.

## Puertos y endpoints fijos

- RTSP video: `rtsp://<ip-jetson>:8554/realsense`
- UDP cliente/IMU/sesión: `5001`
- UDP control `twist`: `5003`

## Compilación en la Jetson

Este proyecto debe compilarse en la Jetson, no en Windows.

1. Instalar dependencias:

```bash
bash scripts/install_dependencies.sh
```

2. Compilar:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

El binario resultante será:

```bash
build/rs_sender
```

Ejemplos de arranque manual:

```bash
./build/rs_sender -file params/config.txt
cd build && ./rs_sender -file ../params/config.txt
```

La aplicacion acepta `-file` y tambien `--file`.

## Servicio systemd

El script genera y habilita automáticamente un servicio `systemd` para arrancar al encender la Jetson.

Uso por defecto:

```bash
bash scripts/install_systemd_service.sh
```

Opciones útiles:

```bash
SERVICE_NAME=rs_sender \
RUN_USER=nvidia \
bash scripts/install_systemd_service.sh
```

Comandos de operación:

```bash
sudo systemctl status rs_sender
sudo systemctl restart rs_sender
sudo journalctl -u rs_sender -f
```

## IP estática en `eth0`

Para fijar `eth0` a `192.168.0.80/24`:

```bash
bash scripts/configure_eth0_static_ip.sh
```

El script intenta usar `NetworkManager` si está disponible. Si no, crea un archivo de `netplan`.

Opciones:

```bash
INTERFACE=eth0 \
ADDRESS=192.168.0.80/24 \
GATEWAY= \
DNS= \
bash scripts/configure_eth0_static_ip.sh
```

Por defecto no define gateway ni DNS, pensado para una red local dedicada.

## Script de dependencias

El script de instalación:
- Instala toolchain de compilación.
- Instala GStreamer y RTSP server.
- Intenta instalar `librealsense2`.
- Descarga cabeceras MAVLink dentro de `third_party/mavlink`.

Uso:

```bash
bash scripts/install_dependencies.sh
```

## Flujo recomendado de despliegue

1. Copiar el repo a la Jetson.
2. Ejecutar `bash scripts/install_dependencies.sh`.
3. Ajustar [params/config.txt](/C:/Users/ajlorenzo/Documents/AAS_projects/Jetson_pasarela/params/config.txt).
4. Compilar con CMake.
5. Ejecutar `bash scripts/install_systemd_service.sh`.
6. Si hace falta, fijar IP con `bash scripts/configure_eth0_static_ip.sh`.

## Notas

- El video se publica siempre por RTSP H264 con timestamp RealSense embebido en SEI.
- La OrangeCube se controla continuamente; si caduca el `twist`, se envía consigna nula.
- El servicio usa `params/config.txt` por defecto.
- Si ya tienes librealsense o MAVLink instalados en otra ruta, puedes ajustar el script o `CMakeLists.txt`.
