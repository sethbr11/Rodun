#ifndef PDF_EXPORT_H
#define PDF_EXPORT_H

#include <unordered_map>
#include <vector>
#include <string>
#include "optimizer.h"

void generatePDF(const std::unordered_map<std::string, std::vector<std::vector<double>>>& results,
                const std::unordered_map<std::string, int>& stockLengths,
                const std::vector<Part>& parts,
                const std::string& outputPath);

#endif // PDF_EXPORT_H