#!/usr/bin/env bash
# -------------------
# This script sets up the BLE server as a systemd service
# Create virtual environment and install requirements with pip install -r requirements.txt before running
# VENV_DIR can be adjusted if you want to use a different location for the virtual environment
# Please run as root: sudo ./install.sh
# -------------------
set -euo pipefail
# Configuration variables
VENV_DIR="/home/ijong/dev/HomeDisplay/.venv"
SERVICE_HOME="/home/ijong/"
CLOUD_PROJECT_DEFAULT="elegant-zodiac-386801"

SERVICE_NAME="homedisplay-ble.service"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_FILE_PATH="/etc/systemd/system/${SERVICE_NAME}"
ENV_FILE_PATH="/etc/default/homedisplay-ble"
BT_OVERRIDE_DIR="/etc/systemd/system/bluetooth.service.d"
BT_OVERRIDE_FILE="${BT_OVERRIDE_DIR}/override.conf"
PYTHON_BIN="${VENV_DIR}/bin/python3"
ADC_PATH_DEFAULT="${SERVICE_HOME}/.config/gcloud/application_default_credentials.json"

# (Optional) For AI: Default values for environment variables
sed -i "s|^GOOGLE_CLOUD_PROJECT=.*$|GOOGLE_CLOUD_PROJECT=${CLOUD_PROJECT_DEFAULT}|" "${ENV_FILE_PATH}"
sed -i "s|^GOOGLE_APPLICATION_CREDENTIALS=.*$|GOOGLE_APPLICATION_CREDENTIALS=${ADC_PATH_DEFAULT}|" "${ENV_FILE_PATH}"

echo "Writing optional environment file: ${ENV_FILE_PATH}"
cat > "${ENV_FILE_PATH}" <<'EOF'
GOOGLE_CLOUD_PROJECT=${CLOUD_PROJECT_DEFAULT}
GOOGLE_APPLICATION_CREDENTIALS=${ADC_PATH_DEFAULT}
EOF

# Check if bluetooth.service is running with --experimental
echo "Ensuring bluetooth.service runs with --experimental"
mkdir -p "${BT_OVERRIDE_DIR}"
cat > "${BT_OVERRIDE_FILE}" <<EOF
[Service]
ExecStart=
ExecStart=${BLUETOOTHD_BIN} --experimental
EOF

# Create systemd service file for the BLE server
echo "Writing systemd unit: ${SERVICE_FILE_PATH}"
cat > "${SERVICE_FILE_PATH}" <<EOF
[Unit]
Description=HomeDisplay Raspberry Pi BLE Server
After=bluetooth.service network-online.target
Wants=network-online.target
Requires=bluetooth.service

[Service]
Type=simple
User=${SERVICE_USER}
Group=${SERVICE_USER}
WorkingDirectory=${SCRIPT_DIR}
ExecStart=${PYTHON_BIN} ${SCRIPT_DIR}/ble_server.py
Restart=always
RestartSec=3
Environment=HOME=${SERVICE_HOME}
EnvironmentFile=-${ENV_FILE_PATH}

[Install]
WantedBy=multi-user.target
EOF

# Reload systemd and restart bluetooth to apply changes
systemctl daemon-reload
systemctl restart bluetooth

# Enable and start the BLE server service
systemctl enable --now "${SERVICE_NAME}"

echo "Done. Check status with: systemctl status ${SERVICE_NAME}"
echo "Follow logs with: journalctl -u ${SERVICE_NAME} -f"