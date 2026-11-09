# Lab 5 — USB HID probe (libusb + sysfs + Qt6 Widgets)

GUI-приложение на C++17 / Qt6 Widgets для Linux (NixOS), которое через
**libusb-1.0** и **sysfs**:

1. перечисляет USB HID-устройства (мышь, клавиатуру и т.п.);
2. выводит для каждого: **VID, PID, класс, протокол, кол-во endpoint'ov**;
3. получает строковые дескрипторы (**производитель, продукт, серийный номер**);
4. отслеживает подключение/отключение устройств (опрос `libusb_get_device_list`
   в отдельном потоке + сравнение снимков).

Основная работа с USB — низкоуровневая (libusb + sysfs). Qt задействован только
как слой GUI (виджеты), без высокоуровневых USB-абстракций.

## Структура

```
lab5/
├── flake.nix            # devShell: cmake, g++, pkg-config, libusb1, qt6.qtbase
├── .envrc               # use flake (direnv)
├── CMakeLists.txt       # Qt6 Widgets + pkg-config(libusb-1.0); AUTOMOC
├── 50-hid.rules         # udev-правило для доступа без root
├── run-root.sh          # запуск под root с сохранением графического окружения
└── src/
    ├── main.cpp         # QApplication, разбор аргументов
    ├── mainwindow.*     # окно: таблица устройств + журнал событий
    ├── usbmonitor.*     # фоновый поток: опрос libusb, разница снимков
    ├── usbprobe.*       # libusb: дескрипторы, endpoint'ы, строки
    ├── sysfs.*          # чтение атрибутов /sys/bus/usb/devices/*
    └── devtable.*       # модель снимка + события add/remove
```

## Сборка

Окружение полностью описывает flake (NixOS):

```bash
direnv allow          # или: nix develop

cmake -S . -B build
cmake --build build
```

## Запуск

```bash
./build/usbhidprobe --interval 1000
#  — вывод таблицы видимых USB-устройств; при вставке/извлечении
#    в журнале появляется запись [+] / [-].
```

> Для старта GUI нужен графический сеанс (X11/Wayland). Без него можно проверить
> инициализацию бездисплейно:
> `QT_QPA_PLATFORM=offscreen ./build/usbhidprobe`.

### Boost：почему падает через `sudo` и как исправить

`sudo` по умолчанию обрезает переменные окружения (в т.ч. `DISPLAY`,
`XAUTHORITY`, `QT_PLUGIN_PATH`). Из-за этого Qt не может открыть графический
сеанс и вызывает `abort()` — программа «крашится».

Как это исправлено в проекте:
- `qt.conf` создаётся рядом с бинарём при сборке и указывает точный путь к
  плагинам Qt — приложение больше не зависит от `QT_PLUGIN_PATH` в окружении.
- В `main.cpp` есть проверка: если графической сессии не видно, программа
  аккуратно пишет сообщение и выходит (без abort).

Как запускать с root (это нужно только для чтения строковых дескрипторов,
т.е. для доступа к usbfs):
```bash
./run-root.sh --interval 1000
# или вручную:
sudo --preserve-env=DISPLAY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR,XAUTHORITY \
     ./build/usbhidprobe
```

> **Wayland/Hyprland:** критичен `XDG_RUNTIME_DIR` (в нём лежит сокет `wayland-N`
> и куки Xwayland). Без него Qt выдаёт «XDG_RUNTIME_DIR is invalid» /
> «Failed to create wl_display», а xcb — «Authorization required» и падает.
> `run-root.sh` пробрасывает `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, `XAUTHORITY`
> и включает платформу `wayland` автоматически.

Если X-сервер закрыт для root (`Authorization required, but no authorization
protocol specified`), можно разрешить локальному root доступ к экрану:
```bash
xhost +SI:localuser:root
sudo ./build/usbhidprobe
```

### Права (usbfs)

Для чтения строковых дескрипторов libusb нужен доступ к `/dev/bus/usb/**`:

```bash
# вариант A — от root (сохраняя графическое окружение):
./run-root.sh

# вариант B — udev-правило для обычного пользователя (без sudo):
sudo cp 50-hid.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
./build/usbhidprobe
```

Без прав программа всё равно выведет VID/PID/класс/протокол/endpoint'ы
(из дескрипторов и sysfs), но строки могут оказаться пустыми — это допустимо.

## Определение HID

Устройство считается HID, если:

- `bDeviceClass == 0x03`, или
- хотя бы у одного интерфейса `bInterfaceClass == 0x03`.

## Определение адреса в sysfs

Для каждого узла берутся `busnum` и `devnum` из `/sys/bus/usb/devices/*` и
сопоставляются с `libusb_get_bus_number()` / `libusb_get_device_address()`.
Затем из этого каталога читаются атрибуты `manufacturer`/`product`/`serial`.
Если строки нечитаемы через libusb (нет прав), используются sysfs-атрибуты.

## Интерфейс

- **Таблица**: Bus:Addr, VID, PID, класс, протокол, endpoint'ы, флаг HID,
  производитель, продукт, серийный номер.
- **Журнал**: время и события `(+) Подключено` / `(-) Отключено`.
- **Строка состояния**: число устройств на шине.