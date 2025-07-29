#include "pdf_export.h"
#include <hpdf.h>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <set>

void custom_error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void *user_data) {
    fprintf(stderr, "Haru PDF ERROR: error_no=0x%04x, detail_no=0x%04x\n",
            (unsigned int)error_no, (unsigned int)detail_no);
}

// Updated function signature to include parts data
void generatePDF(const std::unordered_map<std::string, std::vector<std::vector<double>>>& results,
                const std::unordered_map<std::string, int>& stockLengths,
                const std::vector<Part>& parts, // Add parts data
                const std::string& outputPath) {

    HPDF_Doc pdf = HPDF_New(custom_error_handler, nullptr);
    if (!pdf) return;

    const HPDF_Font font = HPDF_GetFont(pdf, "Helvetica", nullptr);
    const HPDF_Font boldFont = HPDF_GetFont(pdf, "Helvetica-Bold", nullptr);

    for (const auto& [dim, stocks] : results) {
        HPDF_Page page = HPDF_AddPage(pdf);
        HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);

        float pageWidth = HPDF_Page_GetWidth(page);
        float pageHeight = HPDF_Page_GetHeight(page);
        float margin = 40;
        float currentY = pageHeight - margin;

        int stockLen = stockLengths.at(dim);

        // Title
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, boldFont, 20);
        std::string title = "MATERIAL CUTS";
        float titleWidth = HPDF_Page_TextWidth(page, title.c_str());
        HPDF_Page_TextOut(page, (pageWidth - titleWidth) / 2, currentY, title.c_str());
        currentY -= 50;

        // Dimension header
        HPDF_Page_SetFontAndSize(page, boldFont, 14);
        std::string dimHeader = dim + " (" + std::to_string(stockLen) + "\")";
        HPDF_Page_TextOut(page, margin, currentY, dimHeader.c_str());
        currentY -= 40;
        HPDF_Page_EndText(page);

        // Create a mapping from length to all parts with that length for this dimension
        std::unordered_map<double, std::vector<Part>> lengthToPartMap;
        for (const auto& part : parts) {
            if (part.dimension == dim) {
                lengthToPartMap[part.length].push_back(part);
            }
        }

        // Create parts summary for this dimension FIRST - get all unique parts
        std::vector<Part> uniquePartsForDim;
        std::unordered_map<std::string, int> partNumberToID; // Map part_number to ID in summary

        for (const auto& part : parts) {
            if (part.dimension == dim) {
                // Check if we already have this part_number
                bool found = false;
                for (const auto& existingPart : uniquePartsForDim) {
                    if (existingPart.part_number == part.part_number) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    uniquePartsForDim.push_back(part);
                    partNumberToID[part.part_number] = uniquePartsForDim.size(); // 1-based ID
                }
            }
        }

        // Create a counter for each part to track how many we've used
        std::unordered_map<std::string, int> partUsageCount;

        // Calculate scale factor for visual representation
        float maxDrawWidth = pageWidth - 2 * margin - 100; // Leave space for labels
        float scale = maxDrawWidth / stockLen;

        for (size_t stockIndex = 0; stockIndex < stocks.size(); ++stockIndex) {
            if (currentY < margin + 100) { // Need new page
                page = HPDF_AddPage(pdf);
                HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
                currentY = pageHeight - margin;
            }

            // Calculate total used length for this stock
            double totalUsed = 0;
            for (double len : stocks[stockIndex]) {
                totalUsed += len;
            }
            double waste = stockLen - totalUsed;

            // Draw stock representation
            float stockY = currentY;
            float stockX = margin + 80; // Leave space for stock label
            float stockHeight = 40;
            float stockWidth = stockLen * scale;

            // Stock label on the left
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, 10);
            std::string stockLabel = "Stock " + std::to_string(stockIndex + 1);
            HPDF_Page_TextOut(page, margin, stockY - 15, stockLabel.c_str());
            HPDF_Page_EndText(page);

            // Draw main stock rectangle
            HPDF_Page_SetRGBStroke(page, 0, 0, 0);
            HPDF_Page_SetLineWidth(page, 2);
            HPDF_Page_Rectangle(page, stockX, stockY - stockHeight, stockWidth, stockHeight);
            HPDF_Page_Stroke(page);

            // Draw individual parts within the stock
            float currentPartX = stockX;
            float centerY = stockY - stockHeight / 2; // Declare centerY outside the loop

            for (size_t partIndex = 0; partIndex < stocks[stockIndex].size(); ++partIndex) {
                double partLength = stocks[stockIndex][partIndex];
                float partWidth = partLength * scale;

                // Draw part rectangle with subtle fill
                HPDF_Page_SetRGBFill(page, 0.95, 0.95, 0.95);
                HPDF_Page_Rectangle(page, currentPartX, stockY - stockHeight, partWidth, stockHeight);
                HPDF_Page_FillStroke(page);

                // Draw part separator line (except for last part)
                if (partIndex < stocks[stockIndex].size() - 1) {
                    HPDF_Page_SetRGBStroke(page, 0.5, 0.5, 0.5);
                    HPDF_Page_SetLineWidth(page, 1);
                    HPDF_Page_MoveTo(page, currentPartX + partWidth, stockY - stockHeight);
                    HPDF_Page_LineTo(page, currentPartX + partWidth, stockY);
                    HPDF_Page_Stroke(page);
                }

                // Find the correct part for this length and position
                int partID = 1; // Default fallback
                std::string currentPartNumber = "";

                if (lengthToPartMap.find(partLength) != lengthToPartMap.end()) {
                    const auto& partsWithLength = lengthToPartMap[partLength];

                    if (partsWithLength.size() == 1) {
                        // Only one part with this length
                        currentPartNumber = partsWithLength[0].part_number;
                        partID = partNumberToID[currentPartNumber];
                    } else {
                        // Multiple parts with same length - need to distribute them
                        // Calculate total cuts needed for this length across all stocks
                        int totalCutsNeeded = 0;
                        for (const auto& p : partsWithLength) {
                            totalCutsNeeded += p.quantity;
                        }

                        // Count how many cuts of this length we've seen so far
                        int cutsSeenSoFar = 0;
                        for (size_t prevStock = 0; prevStock < stockIndex; ++prevStock) {
                            for (double prevLen : stocks[prevStock]) {
                                if (std::abs(prevLen - partLength) < 0.01) {
                                    cutsSeenSoFar++;
                                }
                            }
                        }
                        // Add cuts from current stock up to current part
                        for (size_t prevPart = 0; prevPart < partIndex; ++prevPart) {
                            if (std::abs(stocks[stockIndex][prevPart] - partLength) < 0.01) {
                                cutsSeenSoFar++;
                            }
                        }

                        // Determine which part this cut belongs to
                        int cumulativeQuantity = 0;
                        for (const auto& p : partsWithLength) {
                            if (cutsSeenSoFar < cumulativeQuantity + p.quantity) {
                                currentPartNumber = p.part_number;
                                partID = partNumberToID[currentPartNumber];
                                break;
                            }
                            cumulativeQuantity += p.quantity;
                        }
                    }
                }

                // Track usage for stock assignments
                partUsageCount[currentPartNumber]++;

                // Add part ID in circle
                float centerX = currentPartX + partWidth / 2;

                // Draw circle for part number
                HPDF_Page_SetRGBFill(page, 1, 1, 1);
                HPDF_Page_SetRGBStroke(page, 0, 0, 0);
                HPDF_Page_SetLineWidth(page, 1);
                HPDF_Page_Circle(page, centerX, centerY, 12);
                HPDF_Page_FillStroke(page);

                // Add part ID text
                HPDF_Page_BeginText(page);
                HPDF_Page_SetFontAndSize(page, font, 8);
                HPDF_Page_SetRGBFill(page, 0, 0, 0);
                std::string partNum = std::to_string(partID);
                float numWidth = HPDF_Page_TextWidth(page, partNum.c_str());
                HPDF_Page_TextOut(page, centerX - numWidth/2, centerY - 3, partNum.c_str());
                HPDF_Page_EndText(page);

                // Add length dimension below the part
                HPDF_Page_BeginText(page);
                HPDF_Page_SetFontAndSize(page, font, 8);
                std::stringstream lenStr;
                lenStr << std::fixed << std::setprecision(2) << partLength << "\"";
                std::string lengthText = lenStr.str();
                float lengthWidth = HPDF_Page_TextWidth(page, lengthText.c_str());
                HPDF_Page_TextOut(page, centerX - lengthWidth/2, stockY - stockHeight - 15, lengthText.c_str());
                HPDF_Page_EndText(page);

                currentPartX += partWidth;
            }

            // Draw waste area if any
            if (waste > 0.1) { // Only show if significant waste
                float wasteWidth = waste * scale;
                HPDF_Page_SetRGBFill(page, 0.8, 0.8, 0.8);
                HPDF_Page_SetRGBStroke(page, 0.6, 0.6, 0.6);
                HPDF_Page_Rectangle(page, currentPartX, stockY - stockHeight, wasteWidth, stockHeight);
                HPDF_Page_FillStroke(page);

                // Add "WASTE" label
                HPDF_Page_BeginText(page);
                HPDF_Page_SetFontAndSize(page, font, 7);
                HPDF_Page_SetRGBFill(page, 0.4, 0.4, 0.4);
                float wasteCenterX = currentPartX + wasteWidth / 2;
                HPDF_Page_TextOut(page, wasteCenterX - 12, centerY - 2, "WASTE");
                HPDF_Page_EndText(page);
            }

            // Add total length dimension above the stock
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, 9);
            HPDF_Page_SetRGBFill(page, 0, 0, 0);
            std::string totalLenText = std::to_string(stockLen) + "\" total";
            HPDF_Page_TextOut(page, stockX, stockY + 10, totalLenText.c_str());
            HPDF_Page_EndText(page);

            // Draw dimension line above stock
            HPDF_Page_SetRGBStroke(page, 0.3, 0.3, 0.3);
            HPDF_Page_SetLineWidth(page, 0.5);
            // Top dimension line
            HPDF_Page_MoveTo(page, stockX, stockY + 5);
            HPDF_Page_LineTo(page, stockX + stockWidth, stockY + 5);
            // End caps
            HPDF_Page_MoveTo(page, stockX, stockY + 2);
            HPDF_Page_LineTo(page, stockX, stockY + 8);
            HPDF_Page_MoveTo(page, stockX + stockWidth, stockY + 2);
            HPDF_Page_LineTo(page, stockX + stockWidth, stockY + 8);
            HPDF_Page_Stroke(page);

            currentY -= 80; // Space between stocks
        }

        // Create stock mapping for summary table
        std::unordered_map<std::string, std::vector<int>> partToStocks; // part_number -> list of stock indices

        // Reset usage counter and build summary by going through each stock again
        partUsageCount.clear();
        for (size_t stockIndex = 0; stockIndex < stocks.size(); ++stockIndex) {
            for (size_t partIndex = 0; partIndex < stocks[stockIndex].size(); ++partIndex) {
                double partLength = stocks[stockIndex][partIndex];

                if (lengthToPartMap.find(partLength) != lengthToPartMap.end()) {
                    const auto& partsWithLength = lengthToPartMap[partLength];

                    std::string currentPartNumber = "";
                    if (partsWithLength.size() == 1) {
                        currentPartNumber = partsWithLength[0].part_number;
                    } else {
                        // Calculate which part this cut belongs to (same logic as above)
                        int cutsSeenSoFar = 0;
                        for (size_t prevStock = 0; prevStock < stockIndex; ++prevStock) {
                            for (double prevLen : stocks[prevStock]) {
                                if (std::abs(prevLen - partLength) < 0.01) {
                                    cutsSeenSoFar++;
                                }
                            }
                        }
                        for (size_t prevPart = 0; prevPart < partIndex; ++prevPart) {
                            if (std::abs(stocks[stockIndex][prevPart] - partLength) < 0.01) {
                                cutsSeenSoFar++;
                            }
                        }

                        int cumulativeQuantity = 0;
                        for (const auto& p : partsWithLength) {
                            if (cutsSeenSoFar < cumulativeQuantity + p.quantity) {
                                currentPartNumber = p.part_number;
                                break;
                            }
                            cumulativeQuantity += p.quantity;
                        }
                    }

                    if (!currentPartNumber.empty()) {
                        partToStocks[currentPartNumber].push_back(static_cast<int>(stockIndex + 1));
                    }
                }
            }
        }

        // Add parts summary table at bottom
        currentY -= 20;
        if (currentY < margin + (uniquePartsForDim.size() * 12) + 80) { // Need new page for table
            page = HPDF_AddPage(pdf);
            HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
            currentY = pageHeight - margin;
        }

        // Parts table header
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, boldFont, 12);
        HPDF_Page_TextOut(page, margin, currentY, "Parts Summary");
        currentY -= 25;

        HPDF_Page_SetFontAndSize(page, boldFont, 10);
        HPDF_Page_TextOut(page, margin, currentY, "ID");
        HPDF_Page_TextOut(page, margin + 25, currentY, "Part #");
        HPDF_Page_TextOut(page, margin + 140, currentY, "Length");
        HPDF_Page_TextOut(page, margin + 185, currentY, "Qty");
        HPDF_Page_TextOut(page, margin + 210, currentY, "Stocks");
        HPDF_Page_TextOut(page, margin + 310, currentY, "Dimension");
        currentY -= 15;
        HPDF_Page_EndText(page);

        // Draw table header line
        HPDF_Page_SetRGBStroke(page, 0, 0, 0);
        HPDF_Page_SetLineWidth(page, 1);
        HPDF_Page_MoveTo(page, margin, currentY);
        HPDF_Page_LineTo(page, pageWidth - margin, currentY);
        HPDF_Page_Stroke(page);
        currentY -= 10;

        // Parts table content
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, 9);

        for (size_t i = 0; i < uniquePartsForDim.size(); ++i) {
            if (currentY < margin + 20) { // Need new page
                HPDF_Page_EndText(page);
                page = HPDF_AddPage(pdf);
                HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
                currentY = pageHeight - margin;

                // Redraw header
                HPDF_Page_BeginText(page);
                HPDF_Page_SetFontAndSize(page, boldFont, 10);
                HPDF_Page_TextOut(page, margin, currentY, "ID");
                HPDF_Page_TextOut(page, margin + 25, currentY, "Part #");
                HPDF_Page_TextOut(page, margin + 140, currentY, "Length");
                HPDF_Page_TextOut(page, margin + 185, currentY, "Qty");
                HPDF_Page_TextOut(page, margin + 210, currentY, "Stocks");
                HPDF_Page_TextOut(page, margin + 310, currentY, "Dimension");
                currentY -= 15;
                HPDF_Page_EndText(page);

                // Draw header line
                HPDF_Page_SetRGBStroke(page, 0, 0, 0);
                HPDF_Page_SetLineWidth(page, 1);
                HPDF_Page_MoveTo(page, margin, currentY);
                HPDF_Page_LineTo(page, pageWidth - margin, currentY);
                HPDF_Page_Stroke(page);
                currentY -= 10;

                HPDF_Page_BeginText(page);
                HPDF_Page_SetFontAndSize(page, font, 9);
            }

            const Part& part = uniquePartsForDim[i];

            // ID (1-based)
            HPDF_Page_TextOut(page, margin, currentY, std::to_string(i + 1).c_str());

            // Part Number (truncate if too long)
            std::string partNum = part.part_number;
            if (partNum.length() > 15) {
                partNum = partNum.substr(0, 12) + "...";
            }
            HPDF_Page_TextOut(page, margin + 25, currentY, partNum.c_str());

            // Length
            std::stringstream lenStr;
            lenStr << std::fixed << std::setprecision(2) << part.length << "\"";
            HPDF_Page_TextOut(page, margin + 140, currentY, lenStr.str().c_str());

            // Quantity
            HPDF_Page_TextOut(page, margin + 185, currentY, std::to_string(part.quantity).c_str());

            // Stock numbers - format nicely and handle overflow
            std::string stockText = "";
            if (partToStocks.find(part.part_number) != partToStocks.end()) {
                const std::vector<int>& stockNums = partToStocks[part.part_number];
                std::stringstream stockStr;
                std::set<int> uniqueStocks(stockNums.begin(), stockNums.end()); // Remove duplicates and sort
                bool first = true;
                for (int stockNum : uniqueStocks) {
                    if (!first) stockStr << ",";
                    stockStr << stockNum;
                    first = false;
                }

                stockText = stockStr.str();
                if (stockText.length() > 12) {
                    // If too many stocks, show count instead
                    stockText = std::to_string(uniqueStocks.size()) + " stocks";
                }
            }
            HPDF_Page_TextOut(page, margin + 210, currentY, stockText.c_str());

            // Dimension
            HPDF_Page_TextOut(page, margin + 310, currentY, part.dimension.c_str());

            currentY -= 12;
        }
        HPDF_Page_EndText(page);
    }

    HPDF_SaveToFile(pdf, outputPath.c_str());
    HPDF_Free(pdf);
}