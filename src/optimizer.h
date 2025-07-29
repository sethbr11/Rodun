#pragma once
#include <vector>
#include <string>

struct Part {
    std::string part_number;
    double length;
    int quantity;
    std::string dimension;
};

void optimizeCuts(const std::vector<Part>& parts, double stockLength,
                  std::vector<std::vector<double>>& result);
