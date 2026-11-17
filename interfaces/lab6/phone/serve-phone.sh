#!/usr/bin/env bash
# HTTPS-сервер для phone/web-bluetooth.html.
# Web Bluetooth требует secure context; self-signed сертификат нужно принять.
set -euo pipefail
cd "$(dirname "$0")"

PORT="${PHONE_PORT:-8443}"

if [[ ! -f cert.pem || ! -f key.pem ]]; then
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout key.pem -out cert.pem -days 365 \
        -subj "/CN=lab6-ble-phone" >/dev/null 2>&1
fi

# Адрес Wi-Fi/Ethernet, через который ноутбук выходит в сеть.
LAN_IP="$(ip route get 1.1.1.1 2>/dev/null \
    | awk '{for (i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}')"
if [[ -z "$LAN_IP" ]]; then
    LAN_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
fi

if ss -ltn 2>/dev/null | grep -q ":${PORT} "; then
    echo "Порт ${PORT} уже занят, останавливаю старый phone/serve.py..."
    pkill -f "python3 serve.py" 2>/dev/null || true
    sleep 0.5
fi

echo "=== Lab6 phone page ==="
echo "IPv4 на ноутбуке:"
ip -4 -o addr show scope global 2>/dev/null \
    | awk '{printf "  %-8s %s\n", $2, $4}' || true
echo
if [[ "$LAN_IP" == 127.* || -z "$LAN_IP" ]]; then
    echo "Не удалось определить LAN IP. Проверьте Wi-Fi/Ethernet." >&2
else
    URL="https://${LAN_IP}:${PORT}/web-bluetooth.html"
    echo "Откройте на телефоне (тот же Wi-Fi/LAN, что и ноутбук):"
    echo "  $URL"
    if command -v qrencode >/dev/null 2>&1; then
        qrencode -t ANSIUTF8 "$URL" || true
    fi
fi
echo
echo "ВАЖНО:"
echo "  - 80.249.84.61 — это публичный IP роутера/провайдера, не ноутбука."
echo "    По нему телефон в той же Wi-Fi сети не подключится без port forwarding."
echo "  - Телефон должен быть в той же подсети, что и ноутбук; иначе используйте"
echo "    phone/web-bluetooth.html локально на телефоне (file://)."
if systemctl is-active --quiet firewall 2>/dev/null; then
    echo "  - Firewall активен: откройте TCP $PORT:"
    echo "      networking.firewall.allowedTCPPorts = [ $PORT ];"
    echo "      sudo nixos-rebuild switch"
    echo "    временно: sudo iptables -I INPUT -p tcp --dport $PORT -j ACCEPT"
fi
echo "  - Если сертификат не принимается, нажмите Advanced/Proceed в Chrome."
echo

python3 serve.py &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

sleep 0.5
if [[ -n "${LAN_IP:-}" && "$LAN_IP" != 127.* ]]; then
    CODE="$(curl -ks -o /dev/null -w '%{http_code}' --max-time 2 \
        "https://${LAN_IP}:${PORT}/" || true)"
    echo "Проверка https://${LAN_IP}:${PORT}/ -> HTTP ${CODE:-?}"
fi

wait "$SERVER_PID"
