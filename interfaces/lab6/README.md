# Lab6 — BLE file transfer over GATT (BlueZ D-Bus + Qt6)

Приложение на C++17 / Qt6 Widgets для Linux, которое передаёт файлы между
двумя BLE-устройствами через **собственный GATT-профиль** и **прямой BlueZ
D-Bus API** (без Qt Bluetooth).

- **Central** и **Peripheral** могут быть и отправителем, и приёмником.
  Central→Peripheral: Central пишет чанки в Data через `AcquireWrite`
  (`write-without-response`).
- Peripheral→Central: Peripheral отправляет чанки через **Data notifications**,
  Central подтверждает их через Control write (ACK).
- Phone: в `phone/web-bluetooth.html` есть Web Bluetooth-клиент для
  Chrome Android — телефон подключается к ноутбуку как Central и умеет
  отправлять/принимать файлы без установки Android-приложения.
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

### Ноутбук A — Peripheral (реклама; приём/отправка)

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

### Ноутбук B — Central (сканирование; отправка/приём)

```bash
./build/lab6ble --role central
```

1. Нажмите **«Сканировать»**.
2. Выберите устройство с именем `lab6` (колонка «Наш сервис» = «да»).
3. Нажмите **«Подключиться к выбранному»**.
4. Выберите файл и нажмите **«Отправить»**.

Headless-вариант (адрес — BD_ADDR периферии):

```bash
./build/lab6ble --role central --headless --auto \
  --peer AA:BB:CC:DD:EE:FF --send /path/to/file.bin
```

Если `--peer` не указан, central автоматически подключится к первому
устройству с нашим сервисом.

### Обратное направление (Peripheral → Central)

1. Central подключается к Peripheral (как выше).
2. На Peripheral откройте вкладку **Peripheral**, выберите файл в блоке
   **«Отправить на central»** и нажмите **«Отправить»**.
3. Central покажет прогресс в блоке **«Принять от peripheral»**.

Peripheral использует Data notifications, Central отвечает ACK через
Control write. Кнопка отправки на Peripheral активируется после того, как
Central подпишется на Data и пришлёт `HELLO` (становится известен MTU).

## Phone (Web Bluetooth)

Ноутбук должен быть в роли Peripheral с включённой рекламой. На телефоне:

1. Запустите `./phone/serve-phone.sh` и откройте выданный HTTPS-URL в
   **Chrome/Edge for Android** (или откройте `phone/web-bluetooth.html`
   как `file://`).
2. Нажмите **«Подключиться к lab6»** и выберите устройство.
3. **Телефон → ноутбук**: выберите файл и нажмите **«Отправить»**.
4. **Ноутбук → телефон**: на ноутбуке во вкладке Peripheral нажмите
   **«Отправить на central»**; телефон покажет прогресс и скачает файл.

Web Bluetooth требует secure context. Самый простой способ:

```bash
./phone/serve-phone.sh
# Телефон: https://<ip-ноутбука>:8443/web-bluetooth.html
```

Скрипт печатает LAN-IP ноутбука (в текущей сети — `10.0.0.105`), URL и QR-код,
создаёт self-signed cert (примите предупреждение в Chrome) и отдаёт страницу
по HTTPS. Публичный `80.249.84.61` — это адрес роутера/провайдера, а не
ноутбука; по нему телефон в той же Wi-Fi не подключится без port forwarding.

Если телефон не открывает IP:

- проверьте, что телефон в той же Wi-Fi/подсети (`10.0.0.0/24`), а не в
  мобильном интернете или гостевой сети;
- не используйте `80.249.84.61`;
- если порт занят старым сервером: `./phone/stop-phone.sh`;
- если включён firewall: откройте TCP 8443;
- сеть вообще не нужна для BLE: скопируйте `phone/web-bluetooth.html` на
  телефон и откройте как `file://` — Web Bluetooth работает без IP
  (если Chrome разрешает secure context для `file://`).

Альтернативы:

- открыть HTML как `file://` (Chrome Android обычно считает это
  безопасным контекстом);
- для HTTP-origin ноутбука включить `chrome://flags/#unsafely-treat-insecure-origin-as-secure`.

iOS Safari Web Bluetooth не поддерживает. Для iPhone можно подключиться к
`lab6` через **nRF Connect**, чтобы проверить GATT-профиль, но передача
файлов с iPhone без нативного приложения недоступна. Android-телефон
работает как BLE Central: подписывается на Data/Control notifications,
шлёт `HELLO`, затем использует тот же протокол START/ACK/FIN/CRC32.

## GATT-профиль

| Объект | UUID | Свойства |
|---|---|---|
| Service | `6c1f0001-5a3e-4b7a-9c2d-7e8f9a0b1c2d` | Primary |
| Data characteristic | `6c1f0002-5a3e-4b7a-9c2d-7e8f9a0b1c2d` | `write-without-response`, `write`, `notify` |
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

### Data: один GATT-write или notification = один чанк

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
| `7 HELLO` | central готов, сообщает peripheral MTU | `transferId[16]` (нули) |

Статусы: `OK`, `RESUME`, `COMPLETE`, `CRC_MISMATCH`, `SIZE_MISMATCH`,
`ERROR`, `ABORTED`.

`HELLO` отправляется Central после подключения и `StartNotify` на Data/Control.
BlueZ передаёт фактический MTU в options `WriteValue`; Peripheral запоминает
его и использует для исходящих Data notifications. Если MTU в options нет,
используется безопасный 23.

Для Peripheral→Central Data-характеристика должна иметь `notify`:
Peripheral отправляет чанки как notifications, Central подписывается на них и
шлёт ACK/START_ACK/FIN_ACK через Control write. Скорость ниже, чем у
`AcquireWrite`, потому что нотификации идут через D-Bus/GATT notify.

### Надёжность и скорость

- Central→Peripheral: `AcquireWrite` на Data, BlueZ отдаёт fd и MTU; запись
  идёт через `send(..., MSG_NOSIGNAL)`, по одному GATT-фрейму.
- Peripheral→Central: Data notifications + ACK через Control write.
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
  журнал. Повторный клик по «Сканировать» (кнопка меняется на
  «Остановить поиск») останавливает поиск.
- **Peripheral**: запуск/остановка рекламы, каталог сохранения, прогресс
  приёма; блок **«Отправить на central»** с выбором файла и прогрессом
  обратной передачи; журнал.
- **Central**: кроме отправки, блок **«Принять от peripheral»** с каталогом
  сохранения и прогрессом входящей передачи.
- Авто-переподключение включено по умолчанию; отключается чекбоксом или
  `--no-reconnect`.
- Тёмная тема **Gruvbox** (Fusion + палитра + QSS); журнал окрашен по
  уровню (info/warn/error).
- `Ctrl+C`/`SIGTERM`/`SIGHUP` завершают приложение штатно через self-pipe
  и `QSocketNotifier`, с остановкой GATT/advertising и disconnect, без
  segfault.

## CLI

```
--role central|peripheral
--adapter hciN
--send FILE               # для peripheral: файл, который отправить central
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
| `test_frame` | кодирование/декодирование чанков и control (включая HELLO), битые/обрезанные кадры, transferId |
| `test_ratemeter` | скорость и ETA |
| `test_dbusmeta` | сигнатуры `a{sa{sv}}`, `a{oa{sa{sv}}}`, `ao` и round-trip ObjectManager |
| `test_transfer` | полная передача central→peripheral, обратная peripheral→central, обрыв+resume, повтор после битого чанка, финальный CRC mismatch, пустой файл |

Loopback-тесты не требуют Bluetooth-адаптера.

### Ручной чек-лист на двух ноутбуках

1. Передать файл 10–100 МБ, сравнить CRC32/`sha256sum`.
2. Оборвать связь на 30–70% (выключить Bluetooth/уйти), включить,
   дождаться resume, проверить CRC.
3. Запустить с `--corrupt-chunk 10`: в журнале должен быть NACK и
   повторная отправка, итоговый файл корректен.
4. Проверить ошибки: адаптер выключен, устройство не найдено, нет прав,
   диск переполнен.
5. Проверить Peripheral→Central: на Peripheral выбрать файл и отправить
   на Central; на Central должен появиться прогресс приёма.
6. Проверить телефон: Peripheral advertising + `./phone/serve-phone.sh`,
   подключиться из Chrome Android, отправить и принять файл.
5. Убедиться, что прогресс/скорость/ETA обновляются.

## Troubleshooting

**Телефон не открывает IP ноутбука**
- В текущей сети LAN-IP ноутбука — `10.0.0.105`, а `80.249.84.61` —
  публичный IP роутера. Используйте URL, который печатает
  `./phone/serve-phone.sh`.
- Телефон и ноутбук должны быть в одной Wi-Fi-подсети; мобильный интернет
  и гостевая сеть с изоляцией клиентов не подойдут.
- Порт 8443 занят: `./phone/stop-phone.sh`, затем снова
  `./phone/serve-phone.sh`.
- Firewall: `networking.firewall.allowedTCPPorts = [ 8443 ];`.
- Если сеть недоступна, скопируйте `phone/web-bluetooth.html` на телефон
  и откройте `file://` — для BLE IP не нужен.

**Оба ноутбука пишут `Connect ...`, а сервис не найден**
- Это значит, что **оба экземпляра запущены в роли Central**. На одном
  ноутбуке обязательно выберите роль **Peripheral** (комбобокс «Роль этого
  ноутбука») или запустите `--role peripheral`.
- На Peripheral в журнале должно быть:
  `LE advertising запущен: name=lab6, type=peripheral, activeInstances=1`.
  Строки `Connect ...` на этой стороне быть не должно.
- На Central после подключения в журнале должны появиться пути
  `GATT: service=...`, `GATT: data=...`, `GATT: control=...`.
- Если на Central всё ещё `сервис ... не найден`, нажмите
  **«Забыть устройство»** (или `bluetoothctl remove <ADDR>`), затем снова
  «Сканировать» и подключитесь.

**`br-connection-key-missing` / `le-connection-abort-by-local`**
- Обычно это старый bond/кэш BlueZ от предыдущих попыток.
- На Central: выберите устройство и нажмите **«Забыть устройство»**.
- Или вручную:
  ```bash
  bluetoothctl remove 14:AC:60:56:D9:5E
  sudo systemctl restart bluetooth
  ```
- На Peripheral удалите адрес Central:
  ```bash
  bluetoothctl remove B8:1E:A4:2D:6C:28
  ```
- После этого заново запустите Peripheral (advertising), затем Central
  (scan → connect). Не запускайте два экземпляра `lab6ble` на одной машине.

**Телефон не видит lab6 в списке Web Bluetooth**
- Перезапустите приложение после обновления. В журнале должно быть:
  `LE advertising запущен: name=lab6, type=peripheral, ... activeInstances=1`.
  Если приложение пишет `lab6ble уже запущен` — закройте старый экземпляр.
- На Android включите **Bluetooth** и **Геолокацию**; Chrome должен иметь
  разрешение **«Устройства поблизости»** (Android 12+). Без Location
  сканирование BLE в Chrome часто возвращает пустой список.
- Обновите страницу без кэша (или откройте в инкогнито). В журнале страницы
  должна быть строка `UI lab6-phone-v5`, кнопка — «Подключиться к lab6».
  Если видите старую страницу с `lab6-ble` — это кэш.
- Проверьте через **nRF Connect**: если `lab6` там виден, а в Chrome нет —
  проблема в разрешениях Chrome/Location. Если не виден и в nRF Connect —
  реклама не доходит: перезапустите Bluetooth
  (`sudo systemctl restart bluetooth`), передёрните адаптер, подойдите ближе,
  убедитесь, что не запущен второй экземпляр приложения.
- Системное меню Bluetooth на телефоне может не показывать BLE-only
  устройства; используйте Web Bluetooth-страницу или nRF Connect.
- Если сеть/IP недоступны, скопируйте `phone/web-bluetooth.html` на телефон
  и откройте как `file://` — для BLE IP не нужен.

**Устройство не видно**
- Убедитесь, что на периферии нажата «Начать рекламу».
- `bluetoothctl show` — адаптер `Powered: yes`.
- `rfkill list` — нет soft/hard block.
- Имя в рекламе — `lab6`; 128-битный UUID занимает большую часть
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
- Phone-клиент — Web Bluetooth в Chrome/Edge for Android (iOS Safari не умеет).
- Peripheral→Central идёт через notifications + D-Bus PropertiesChanged и
  медленнее, чем Central→Peripheral через `AcquireWrite`.
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
│   ├── gruvbox.*
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
├── phone/
│   ├── web-bluetooth.html   # Chrome Android Web Bluetooth client
│   ├── serve-phone.sh       # HTTPS-сервер для secure context
│   └── serve.py
└── tests/
    ├── test_crc32.cpp
    ├── test_frame.cpp
    ├── test_ratemeter.cpp
    ├── test_dbusmeta.cpp
    └── test_transfer.cpp
```
