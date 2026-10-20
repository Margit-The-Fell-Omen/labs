#include "comparison.h"

#include "strutil.h"

DriveType detectType(const TypeSignals& s)
{
    // NVMe is by definition flash memory.
    if (s.isNvme)
        return DriveType::SSD;

    int ssdVotes = 0;
    int hddVotes = 0;

    if (s.seekPenaltyKnown) {
        if (s.seekPenalty)
            hddVotes += 2;
        else
            ssdVotes += 1;
    }

    if (s.trimKnown) {
        if (s.trimSupported)
            ssdVotes += 2;
        else
            hddVotes += 1;
    }

    if (s.rpmKnown && s.rpm > 0)
        hddVotes += 2;
    else if (s.seekPenaltyKnown && !s.seekPenalty && s.rpm == 0)
        ssdVotes += 1;

    if (hddVotes > ssdVotes)
        return DriveType::HDD;
    if (ssdVotes > hddVotes)
        return DriveType::SSD;
    return DriveType::Unknown;
}

std::wstring typeDetail(const TypeSignals& s)
{
    std::wstring parts;
    auto add = [&parts](const std::wstring& p) {
        if (!parts.empty())
            parts += L", ";
        parts += p;
    };

    if (s.isNvme)
        add(widen("NVMe"));

    if (s.seekPenaltyKnown)
        add(wformat(L"SeekPenalty=%ls", s.seekPenalty ? L"да" : L"нет"));
    if (s.trimKnown)
        add(wformat(L"TRIM=%ls", s.trimSupported ? L"да" : L"нет"));
    if (s.rpmKnown && s.rpm > 0)
        add(wformat(L"RPM=%u", s.rpm));

    if (parts.empty())
        return widen("недостаточно данных");
    return parts;
}

namespace {

std::wstring temperatureString(const StorageDevice& d)
{
    if (!d.hasTemperature)
        return widen("N/A");
    return wformat(L"%d °C", d.temperatureC);
}

std::wstring wearString(const StorageDevice& d)
{
    if (!d.hasReliability || d.wearPercent < 0)
        return widen("N/A");
    return wformat(L"%d%%", d.wearPercent);
}

std::wstring hoursString(const StorageDevice& d)
{
    if (!d.hasReliability || d.powerOnHours == 0)
        return widen("N/A");
    return formatHours(d.powerOnHours);
}

std::wstring rpmOrTrim(const StorageDevice& d)
{
    if (d.type == DriveType::HDD)
        return formatRpm(d.rpm, d.rpmKnown);
    if (d.isNvme)
        return widen("NVMe (флеш-память)");
    if (!d.trimKnown)
        return widen("N/A");
    return d.trimSupported ? widen("Поддерживается") : widen("Нет");
}

std::wstring ataRegistersString(const StorageDevice& d)
{
    if (!d.ataIdentifyOk)
        return widen("N/A");
    const std::wstring base = wformat(L"Status=0x%02X Error=0x%02X Word217=0x%04X",
                                      d.ataStatusRegister, d.ataErrorRegister,
                                      d.ataRotationRateWord);
    return d.ataDirectRegister ? base + widen("  [прямые порты 0x1F0/0x3F0]")
                               : base + widen("  [через IOCTL]");
}

std::wstring nvmeRegistersString(const StorageDevice& d)
{
    if (!d.isNvme)
        return widen("N/A");
    if (d.healthOk)
        return widen("Identify (CNS=1) + SMART/Health log прочитаны");
    return widen("NVMe admin команды доступны");
}

std::wstring healthString(const StorageDevice& d)
{
    if (!d.healthOk && !d.hasReliability && !d.hasTemperature)
        return widen("N/A");

    bool warn = false;
    if (d.wearPercent > 90)
        warn = true;
    if (d.hasTemperature && d.temperatureC > 75)
        warn = true;
    if (d.readErrorsUncorrected > 0)
        warn = true;
    return warn ? widen("Предупреждение") : widen("Нормально");
}

} // namespace

std::vector<CompareRow> compareDevices(const StorageDevice& left, const StorageDevice& right)
{
    std::vector<CompareRow> rows;

    auto add = [&rows](const std::wstring& label, const std::wstring& l, const std::wstring& r,
                       int flags = CompareRow::None) {
        CompareRow row;
        row.label = label;
        row.left = l;
        row.right = r;
        row.flags = flags;
        rows.push_back(row);
    };

    add(widen("Тип накопителя"), driveTypeToString(left.type), driveTypeToString(right.type));

    add(widen("Модель"),
        left.model.empty() ? widen("N/A") : left.model,
        right.model.empty() ? widen("N/A") : right.model);

    add(widen("Серийный номер"), left.serialNumber, right.serialNumber);

    add(widen("Объём"), formatBytes(left.sizeBytes), formatBytes(right.sizeBytes));

    add(widen("Интерфейс"),
        left.busTypeName.empty() ? widen("N/A") : left.busTypeName,
        right.busTypeName.empty() ? widen("N/A") : right.busTypeName);

    add(widen("Скорость / TRIM"), rpmOrTrim(left), rpmOrTrim(right));

    int tempFlags = CompareRow::None;
    if (left.hasTemperature && right.hasTemperature) {
        if (left.temperatureC < right.temperatureC)
            tempFlags |= CompareRow::LeftBetter;
        else if (right.temperatureC < left.temperatureC)
            tempFlags |= CompareRow::RightBetter;
    }
    add(widen("Температура"), temperatureString(left), temperatureString(right), tempFlags);

    add(widen("Часы работы"), hoursString(left), hoursString(right));

    int wearFlags = CompareRow::None;
    if (left.hasReliability && right.hasReliability && left.wearPercent >= 0 && right.wearPercent >= 0) {
        if (left.wearPercent < right.wearPercent)
            wearFlags |= CompareRow::LeftBetter;
        else if (right.wearPercent < left.wearPercent)
            wearFlags |= CompareRow::RightBetter;
    }
    add(widen("Износ"), wearString(left), wearString(right), wearFlags);

    add(widen("Состояние"), healthString(left), healthString(right));

    add(widen("Регистры ATA (IDENTIFY)"), ataRegistersString(left), ataRegistersString(right));
    add(widen("Регистры NVMe (admin/SMART)"), nvmeRegistersString(left), nvmeRegistersString(right));

    add(widen("Основание определения типа"), left.typeDetail, right.typeDetail);

    return rows;
}

std::vector<Advice> recommendationsFor(const StorageDevice& device)
{
    std::vector<Advice> out;

    if (device.type == DriveType::Unknown)
        out.push_back({Advice::Info, device.diskNumber,
                       widen("Тип накопителя не удалось определить однозначно.")});

    if (device.hasReliability && device.wearPercent > 90)
        out.push_back({Advice::Critical, device.diskNumber,
                       widen("Износ более 90% — рекомендуется замена накопителя.")});
    else if (device.hasReliability && device.wearPercent > 70)
        out.push_back({Advice::Warning, device.diskNumber,
                       widen("Износ более 70% — следите за состоянием накопителя.")});

    if (device.hasTemperature && device.temperatureC > 75)
        out.push_back({Advice::Warning, device.diskNumber,
                       widen("Температура выше 75 °C — возможен перегрев.")});

    if (device.readErrorsUncorrected > 0)
        out.push_back({Advice::Warning, device.diskNumber,
                       L"Обнаружены некорректируемые ошибки: "
                           + std::to_wstring(device.readErrorsUncorrected) + L"."});

    if (!device.healthOk && !device.hasReliability)
        out.push_back({Advice::Info, device.diskNumber,
                       widen("S.M.A.R.T./SMART-данные недоступны для этого накопителя.")});

    return out;
}

std::vector<Advice> comparativeAdvice(const StorageDevice& left, const StorageDevice& right)
{
    std::vector<Advice> out;

    if (left.type != DriveType::Unknown && right.type != DriveType::Unknown
        && left.type != right.type) {
        const StorageDevice& ssd = (left.type == DriveType::SSD) ? left : right;
        const StorageDevice& hdd = (left.type == DriveType::HDD) ? left : right;
        out.push_back({Advice::Info, -1,
                       wformat(L"%ls (SSD) обычно обеспечивает лучшую скорость"
                               L" произвольного доступа и меньшую задержку по сравнению"
                               L" с %ls (HDD).",
                               ssd.displayName().c_str(), hdd.displayName().c_str())});
    }

    if (left.hasReliability && right.hasReliability && left.wearPercent >= 0 && right.wearPercent >= 0) {
        const int diff = left.wearPercent - right.wearPercent;
        const int absDiff = diff < 0 ? -diff : diff;
        if (absDiff >= 20) {
            const StorageDevice& fresher = diff < 0 ? left : right;
            out.push_back({Advice::Info, -1,
                           wformat(L"У накопителя с меньшим износом (%ls) больше"
                                   L" остаточного ресурса.",
                                   fresher.displayName().c_str())});
        }
    }

    return out;
}