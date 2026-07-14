#!/bin/bash
set -e
cd /opt/xingyu-backend

echo "==> Installing Docker if needed..."
if ! command -v docker >/dev/null 2>&1; then
  curl -fsSL https://get.docker.com | sh
  systemctl enable docker
  systemctl start docker
fi

if ! docker compose version >/dev/null 2>&1; then
  apt-get update
  apt-get install -y docker-compose-plugin
fi

echo "==> Building and starting services..."
docker compose down || true
docker compose up -d --build

echo "==> Done. Test:"
echo "  curl http://127.0.0.1/health"
echo "  curl http://$(curl -s ifconfig.me)/health"
