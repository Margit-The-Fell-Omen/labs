#!/usr/bin/env bash
# Запуск lab6ble с правами root, сохраняя графическое окружение.
#
# На большинстве систем GATT-сервер и LE-advertising регистрируются через
# BlueZ от обычного пользователя. Если D-Bus/BlueZ возвращает NotAuthorized
# или AccessDenied — используйте этот скрипт.
#
# sudo по умолчанию вырезает DISPLAY/WAYLAND_DISPLAY/XDG_RUNTIME_DIR/XAUTHORITY
# и QT_*; без них Qt-приложение под root не сможет открыть графический сеанс.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/build/lab6ble"

if [[ ! -x "$BIN" ]]; then
    echo "Не найден $BIN. Сначала соберите: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

# Wayland (Hyprland и др.).
if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    export WAYLAND_DISPLAY
    if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then export XDG_RUNTIME_DIR; fi
    if [[ -z "${QT_QPA_PLATFORM:-}" ]]; then export QT_QPA_PLATFORM=wayland; fi
fi

# X11/Xwayland.
if [[ -z "${XAUTHORITY:-}" && -r "$HOME/.Xauthority" ]]; then
    export XAUTHORITY="$HOME/.Xauthority"
fi
if [[ -n "${DISPLAY:-}" ]]; then export DISPLAY; fi

KEEP=()
for var in DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR XAUTHORITY \
           QT_QPA_PLATFORM QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH \
           DBUS_SESSION_BUS_ADDRESS DBUS_SYSTEM_BUS_ADDRESS HOME; do
    if [[ -n "${!var:-}" ]]; then KEEP+=("$var"); fi
done

ENV_ARGS=()
for var in "${KEEP[@]}"; do ENV_ARGS+=(--preserve-env="$var"); done

exec sudo "${ENV_ARGS[@]}" "$BIN" "$@"
