#pragma once

#include "comparison.h"
#include "storage.h"

#include <string>
#include <vector>

bool exportReportHtml(const std::wstring& filePath,
                      const StorageDevice& left,
                      const StorageDevice& right,
                      const std::vector<CompareRow>& rows,
                      const std::vector<Advice>& advices,
                      std::wstring& errorMessage);
