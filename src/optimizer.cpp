#include "optimizer.h"
#include <algorithm>

// Optimize cuts for a vector of parts with given stock length.
// Uses double precision for lengths and returns cuts in double.
void optimizeCuts(const std::vector<Part>& parts, double stockLength, std::vector<std::vector<double>>& result) {
    std::vector<double> allParts;
    for (const auto& part : parts) {
        for (int i = 0; i < part.quantity; ++i)
            allParts.push_back(part.length);
    }

    std::ranges::sort(allParts, std::greater<>());

    for (double partLen : allParts) {
        bool placed = false;
        for (auto& stock : result) {
            double used = 0.0;
            for (double p : stock) used += p;
            if (used + partLen <= stockLength) {
                stock.push_back(partLen);
                placed = true;
                break;
            }
        }
        if (!placed) result.push_back({ partLen });
    }
}
