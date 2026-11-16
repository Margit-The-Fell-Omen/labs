#include "devtable.h"

#include <cstdio>
#include <sstream>

namespace devtable {

std::string DevInfo::summaryLine() const {
    std::ostringstream os;
    os << static_cast<unsigned>(key.bus) << ":" << static_cast<unsigned>(key.addr)
       << " vid=0x" << std::hex << vid
       << " pid=0x" << pid << std::dec
       << " class=0x" << std::hex << static_cast<unsigned>(devClass) << std::dec
       << " proto=" << static_cast<unsigned>(devProtocol)
       << " ep=" << endpointCount
       << (isHid ? " HID" : " nHID");

    if (!manufacturer.empty()) os << " mfr=\"" << manufacturer << '"';
    if (!product.empty()) os << " prod=\"" << product << '"';
    if (!serial.empty()) os << " serial=\"" << serial << '"';
    return os.str();
}

Diff diff(const Snapshot& before, const Snapshot& after) {
    Diff d;
    for (const auto& kv : after)
        if (before.find(kv.first) == before.end())
            d.added.push_back(kv.second);
    for (const auto& kv : before)
        if (after.find(kv.first) == after.end())
            d.removed.push_back(kv.second);
    return d;
}

}  // namespace devtable