#include "devtable.h"

#include <cstdio>
#include <sstream>

namespace devtable {

std::string DevInfo::summaryLine() const {
    // Формат без экранирующих кодов: удобен и для файла, и для --once.
    std::ostringstream os;
    os << static_cast<unsigned>(key.bus) << ":" << static_cast<unsigned>(key.addr)
       << " vid=0x" << std::hex << vid
       << " pid=0x" << pid << std::dec
       << " class=0x" << std::hex << static_cast<unsigned>(devClass) << std::dec
       << " proto=" << static_cast<unsigned>(devProtocol)
       << " ep=" << endpointCount
       << (isHid ? " HID" : " nHID");

    // Строковые дескрипторы: показываем только непустые.
    if (!manufacturer.empty()) os << " mfr=\"" << manufacturer << '"';
    if (!product.empty()) os << " prod=\"" << product << '"';
    if (!serial.empty()) os << " serial=\"" << serial << '"';
    return os.str();
}

Diff diff(const Snapshot& before, const Snapshot& after) {
    Diff d;
    // Устройства, которых не было в `before` — добавлены.
    for (const auto& kv : after)
        if (before.find(kv.first) == before.end())
            d.added.push_back(kv.second);
    // Устройства, которых больше нет в `after` — удалены.
    for (const auto& kv : before)
        if (after.find(kv.first) == after.end())
            d.removed.push_back(kv.second);
    return d;
}

}  // namespace devtable