# Lab6 — BLE file transfer over GATT (BlueZ D-Bus + Qt6)

Приложение на C++17 / Qt6 Widgets для Linux, которое передаёт файлы между
двумя BLE-устройствами через **собственный GATT-профиль** и **прямой BlueZ
D-Bus API** (без Qt Bluetooth).

- **Central** — отправитель: сканирует, подключается, пишет чанки в Data
  через `AcquireWrite` (`write-without-response`).
- **Peripheral** — приёмник: публикует GATT-сервис через `GattManager1`,
  запускает LE-advertising и принимает чанки.
- **CRC32** каждого чанка и всего файла.
- **ACK** от приёмника, окно неподтверждённых байт, повторная отправка.
- **Прогресс, скорость, ETA** в GUI.
- **Разрыв связи и resume**: `.part` + sidecar JSON, продолжение с offset.
- Журнал событий в GUI, stderr и файл.

## Проверенная среда

- NixOS, BlueZ 5.84/5.87, Qt 6.12 (nixpkgs unstable).
- Адаптер с ролями `central` и `peripheral`.
- Наличие D-Bus интерфейсов:
  - `org.bluez.GattManager1`
  - `org.bluez.LEAdvertisingManager1`

Проверить:

```bash
bluetoothctl show | grep -E 'Powered|Roles'
busctl introspect org.bluez /org/bluez/hci0 org.bluez.GattManager1
busctl introspect org.bluez /org/bluez/hci0 org.bluez.LEAdvertisingManager1
```

## Сборка

```bash
direnv allow          # или: nix develop (для ещё не добавленного в git каталога: nix develop path:.)

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Без Nix:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Нужны: Qt6 Core/Widgets/DBus/Test, CMake >= 3.16, компилятор с C++17.

## Быстрый запуск на двух ноутбуках

### Ноутбук A — приёмник (Peripheral)

```bash
./build/lab6ble --role peripheral
```

1. Выберите каталог сохранения.
2. Нажмите **«Начать рекламу»**.
3. В журнале появятся строки о регистрации GATT и запуске advertising.

Headless-вариант:

```bash
./build/lab6ble --role peripheral --headless --auto --out ~/Downloads/lab6
```

### Ноутбук B — отправитель (Central)

```bash
./build/lab6ble --role central
```

1. Нажмите **«Сканировать»**.
2. Выберите устройство с именем `lab6-ble` (колонка «Наш сервис» = «да»).
3. Нажмите **«Подключиться к выбранному»**.
4. Выберите файл и нажмите **«Отправить»**.

Headless-вариант (адрес — BD_ADDR периферии):

```bash
./build/lab6ble --role central --headless --auto \
  --peer AA:BB:CC:DD:EE:FF --send /path/to/file.bin
```

Если `--peer` не указан, central автоматически подключится к первому
устройству с нашим сервисом.

## GATT-профиль

| Объект | UUID | Свойства |
|---|---|---|
| Service | `6c1f0001-5a3e-4b7a-9c2d-7e8f9a0b1c2d` | Primary |
| Data characteristic | `6c1f0002-5a3e-4b7a-9c2d-7e8f9a0b1c2d` | `write-without-response`, `write` |
| Control characteristic | `6c1f0003-5a3e-4b7a-9c2d-7e8f9a0b1c2d` | `read`, `write`, `notify` |

D-Bus объекты приложения:

```
/org/lab6/ble                         ObjectManager
/org/lab6/ble/service0                GattService1
/org/lab6/ble/service0/char_data      GattCharacteristic1
/org/lab6/ble/service0/char_ctrl      GattCharacteristic1
/org/lab6/ble/advert                  LEAdvertisement1
```

## Протокол

Все числа — little-endian.

### Data: один GATT-write = один чанк

```
[u64 offset][u32 crc32(payload)][payload]
```

- `offset` — абсолютное смещение в файле.
- `crc32` — CRC-32 IEEE 802.3 полезной нагрузки.
- Приёмник проверяет CRC каждого чанка.
- `offset < expected` — дубликат, игнорируется.
- `offset > expected` — разрыв; приёмник отправляет NACK с ожидаемым offset.

### Control

```
[u8 type][u16 bodyLen][body]
```

| Type | Назначение | Body |
|---|---|---|
| `1 START` | старт/резюме | `transferId[16]`, `u64 size`, `u16 chunk`, `u32 fileCrc32`, `u16 nameLen`, `name` |
| `2 START_ACK` | ответ приёмника | `transferId[16]`, `u8 status`, `u64 offset`, `u16 mtu`, `u16 chunk` |
| `3 ACK` | подтверждение | `transferId[16]`, `u64 ackedOffset`, `u8 status` |
| `4 FIN` | конец данных | `transferId[16]`, `u64 size`, `u32 fileCrc32` |
| `5 FIN_ACK` | результат CRC | `transferId[16]`, `u8 status`, `u32 fileCrc32` |
| `6 ABORT` | отмена | `transferId[16]`, `u8 status`, `u16 reasonLen`, `reason` |

Статусы: `OK`, `RESUME`, `COMPLETE`, `CRC_MISMATCH`, `SIZE_MISMATCH`,
`ERROR`, `ABORTED`.

### Надёжность и скорость

- `AcquireWrite` на Data: BlueZ отдаёт fd и MTU; запись идёт через
  `send(..., MSG_NOSIGNAL)`, по одному GATT-фрейму.
- Payload чанка = `MTU - 3 - 12` (12 байт заголовка), но не больше 4096.
- Если `AcquireWrite` недоступен, используется `WriteValue` с
  `type=command` (Write Without Response).
- ACK отправляется каждые **8 КиБ** или по хвосту файла.
- Окно отправителя — **64 КиБ** (`--window`).
- Таймаут ACK — 5 секунд; отправитель повторяет с последнего
  подтверждённого offset.
- `transferId` детерминирован: SHA-256(путь + размер + mtime), первые
  16 байт. Поэтому резюме возможно после перезапуска отправителя.

### Resume

Приёмник пишет:

```
<outDir>/<name>.part
<outDir>/<name>.part.json
```

В sidecar: `transferId`, имя, размер, chunk, `received`, CRC.

При повторном `START` с тем же `transferId` и размером приёмник:

1. открывает `.part`;
2. обрезает до последнего сохранённого `received`;
3. возвращает `START_ACK` со статусом `RESUME` и текущим offset.

Если итоговый файл уже существует и его CRC32 совпадает с CRC из `START`,
приёмник возвращает `COMPLETE`, и передача не начинается.

При несовпадении финального CRC32 `.part` удаляется, чтобы повторная
отправка началась с нуля.

## GUI

- **Central**: таблица устройств (имя, адрес, RSSI, наличие сервиса),
  подключение, выбор файла, отправка/отмена, прогресс, скорость, ETA,
  журнал.
- **Peripheral**: запуск/остановка рекламы, каталог сохранения, прогресс
  приёма, скорость, ETA, журнал.
- Авто-переподключение включено по умолчанию; отключается чекбоксом или
  `--no-reconnect`.

## CLI

```
--role central|peripheral
--adapter hciN
--send FILE
--out DIR
--peer BD_ADDR|/org/bluez/hciN/dev_...
--window BYTES
--chunk BYTES
--corrupt-chunk N     # отладка: испортить N-й чанк
--scan-timeout MS
--log FILE
--headless
--auto
--no-reconnect
--verbose
```

## Права

В проверенной среде (NixOS, BlueZ 5.84/5.87) регистрация GATT и advertising
работают **от обычного пользователя**. Если BlueZ возвращает
`org.bluez.Error.NotAuthorized` или `AccessDenied`:

```bash
./run-root.sh --role peripheral
# или:
sudo --preserve-env=DISPLAY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR,XAUTHORITY \
     ./build/lab6ble
```

`run-root.sh` сохраняет Wayland/X11-переменные, потому что `sudo` их
вырезает, и Qt без них не открывает графический сеанс.

Для доступа к HCI/BlueZ на некоторых дистрибутивах также нужны:
- пользователь в группе `bluetooth`;
- polkit-правило для `org.bluez`;
- либо запуск от root.

## Тесты

```bash
ctest --test-dir build --output-on-failure
```

| Тест | Что проверяет |
|---|---|
| `test_crc32` | эталонный вектор `"123456789"` → `0xCBF43926`, пустые данные, инкрементальность |
| `test_frame` | кодирование/декодирование чанков и control, битые/обрезанные кадры, transferId |
| `test_ratemeter` | скорость и ETA |
| `test_dbusmeta` | сигнатуры `a{sa{sv}}`, `a{oa{sa{sv}}}`, `ao` и round-trip ObjectManager |
| `test_transfer` | полная передача, обрыв+resume, повтор после битого чанка, финальный CRC mismatch, пустой файл |

Loopback-тесты не требуют Bluetooth-адаптера.

### Ручной чек-лист на двух ноутбуках

1. Передать файл 10–100 МБ, сравнить CRC32/`sha256sum`.
2. Оборвать связь на 30–70% (выключить Bluetooth/уйти), включить,
   дождаться resume, проверить CRC.
3. Запустить с `--corrupt-chunk 10`: в журнале должен быть NACK и
   повторная отправка, итоговый файл корректен.
4. Проверить ошибки: адаптер выключен, устройство не найдено, нет прав,
   диск переполнен.
5. Убедиться, что прогресс/скорость/ETA обновляются.

## Troubleshooting

**Устройство не видно**
- Убедитесь, что на периферии нажата «Начать рекламу».
- `bluetoothctl show` — адаптер `Powered: yes`.
- `rfkill list` — нет soft/hard block.
- Имя в рекламе — `lab6-ble`; 128-битный UUID занимает большую часть
  пакета, поэтому остальные поля сокращены.

**`RegisterApplication: ... NotAuthorized` / `AccessDenied`**
- Запустите через `./run-root.sh`.
- Или настройте D-Bus/polkit-политику для `org.bluez`.

**`AcquireWrite недоступен`**
- Приложение автоматически переходит на `WriteValue` с `type=command`.
- Скорость будет ниже; это нормально для старых BlueZ/адаптеров.

**`StartNotify` не работает**
- Проверьте, что Control имеет флаг `notify`.
- `btmon` покажет ATT-ошибки.

**Устройство «зависло» в BlueZ**
```bash
bluetoothctl remove AA:BB:CC:DD:EE:FF
```
и повторите скан/подключение.

**Сброс зависшей передачи**
- В GUI нажмите «Отменить».
- Или перезапустите приложение; `.part` и sidecar сохранятся для resume.

## Ограничения

- Только Linux/BlueZ (D-Bus); Windows/macOS не поддерживаются.
- Одна передача за раз.
- Нет шифрования/бонинга: протокол рассчитан на лабораторный стенд.
- CRC32 — контроль целостности, не криптографическая подпись.
- GUI выполняет файловый I/O в основном потоке; для очень больших файлов
  предварительный подсчёт CRC32 занимает время.

## Структура

```
lab6/
├── flake.nix
├── CMakeLists.txt
├── run-root.sh
├── README.md
├── src/
│   ├── main.cpp
│   ├── appconfig.*
│   ├── logging.*
│   ├── mainwindow.*
│   ├── ble/
│   │   ├── bluezconst.h
│   │   ├── dbusutil.*
│   │   ├── gattserver.*
│   │   └── central.*
│   ├── protocol/
│   │   ├── crc32.*
│   │   └── frame.*
│   └── transfer/
│       ├── sender.*
│       ├── receiver.*
│       ├── resumestore.*
│       └── ratemeter.*
└── tests/
    ├── test_crc32.cpp
    ├── test_frame.cpp
    ├── test_ratemeter.cpp
    ├── test_dbusmeta.cpp
    └── test_transfer.cpp
```
