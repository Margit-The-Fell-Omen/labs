#!/usr/bin/env bash
# Останавливает только HTTPS-сервер phone/serve.py, запущенный этим репозиторием.
set -euo pipefail
if pgrep -f "python3 serve.py" >/dev/null 2>&1; then
    pkill -f "python3 serve.py" || true
    sleep 0.4
    echo "phone HTTPS server stopped"
else
    echo "phone HTTPS server is not running"
fi
