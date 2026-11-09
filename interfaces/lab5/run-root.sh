#!/usr/bin/env bash
# Запуск usbhidprobe с правами root, сохраняя графическое окружение.
#
# Проблема: sudo обрезает DISPLAY/WAYLAND_DISPLAY/XDG_RUNTIME_DIR/XAUTHORITY
# (и QT_*). Для Qt-приложения под root это приводит к:
#   - "XDG_RUNTIME_DIR is invalid or not set" + "Failed to create wl_display"
#     (Wayland: не виден сокет wayland-N),
#   - "Authorization required" / "could not connect to display :0"
#     (xcb: нет X11-токена XAUTHORITY),
#   и затем "Could not load the Qt platform plugin" -> abort().
#
# Здесь мы пробрасываем все нужные переменные. Особый случай — Hyprland/Wayland:
# критичен XDG_RUNTIME_DIR (/run/user/<uid>), где лежат сокеты и Xwayland-auth;
# root имеет право читать этот каталог.
#
# Использование:
#   ./run-root.sh [аргументы usbhidprobe]
#
# Совет: строковые дескрипторы — единственное, что требует root (usbfs).
# Если применить 50-hid.rules, sudo вообще не нужен — просто ./build/usbhidprobe.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/build/usbhidprobe"

if [[ ! -x "$BIN" ]]; then
    echo "Не найден $BIN. Сначала соберите: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

# --- Wayland (Hyprland и др.): сокет и runtime-каталог. ---
if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    export WAYLAND_DISPLAY
    # XDG_RUNTIME_DIR обязателен для поиска сокета wayland-N.
    if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then export XDG_RUNTIME_DIR; fi
    # Предпочитаем Wayland-платформу Qt, если не задана другая.
    if [[ -z "${QT_QPA_PLATFORM:-}" ]]; then export QT_QPA_PLATFORM=wayland; fi
fi

# --- X11/Xwayland: токен авторизации (куки). ---
if [[ -z "${XAUTHORITY:-}" && -r "$HOME/.Xauthority" ]]; then
    export XAUTHORITY="$HOME/.Xauthority"
fi
# Fallback для Wayland/Xwayland: авторизация в runtime-каталоге сессии.
if [[ -z "${XAUTHORITY:-}" || ! -r "${XAUTHORITY:-}" ]]; then
    for f in "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* "$XDG_RUNTIME_DIR"/*-xauth \
             "$XDG_RUNTIME_DIR"/*.xauth; do
        [[ -r "$f" ]] && { export XAUTHORITY="$f"; break; }
    done
fi
if [[ -n "${DISPLAY:-}" ]]; then export DISPLAY; fi

# --- Собираем список переменных для сохранения при повышении. ---
KEEP=()
for var in DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR XAUTHORITY \
           QT_QPA_PLATFORM QT_PLUGIN_PATH; do
    if [[ -n "${!var:-}" ]]; then KEEP+=("$var"); fi
done
ENV_ARGS=()
for var in "${KEEP[@]}"; do ENV_ARGS+=(--preserve-env="$var"); done

sudo "${ENV_ARGS[@]}" "$BIN" "$@"