#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <GLFW/glfw3.h>

#include "app.h"

#include <ranges>

#include "optimizer.h"
#include "pdf_export.h"
#include "utils.h"

void App::run() {
    // GLFW + OpenGL + ImGui setup
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Important on macOS

    const char* glsl_version = "#version 410 core";
    GLFWwindow* window = glfwCreateWindow(800, 600, "Rodun", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // App state
    std::vector<Part> parts;
    std::unordered_map<std::string, int> stockLengths; // stock length per dimension

    double inputLength = 0.0;
    int inputQty = 0;
    char inputNum[25] = "";
    bool showResults = false;

    // Optimization results per dimension
    std::unordered_map<std::string, std::vector<std::vector<double>>> optimizationResults;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Material Optimizer");

        // Input fields for new part
        ImGui::InputText("Part Number (Optional)", inputNum, sizeof(inputNum));

        // Dimensions dropdown and custom input
        const char* materialDims[] = {
            "1/2 x 1/2", "1 x 1/2", "1 x 1", "1-1/2 x 1",
            "1-1/2 x 1-1/2", "2 x 1", "2 x 1-1/2", "2 x 2",
            "2-1/2 x 2", "2-1/2 x 2-1/2", "Custom"
        };
        static int currentDim = 0;
        static char customDim[32] = "";
        static std::string selectedDim = materialDims[0];

        if (ImGui::BeginCombo("##material_dims", selectedDim.c_str()))
        {
            for (int n = 0; n < IM_ARRAYSIZE(materialDims); n++)
            {
                bool is_selected = (currentDim == n);
                if (ImGui::Selectable(materialDims[n], is_selected))
                {
                    currentDim = n;
                    if (n != IM_ARRAYSIZE(materialDims) - 1) // Not "Custom"
                    {
                        selectedDim = materialDims[n];
                        customDim[0] = '\0';
                    }
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, 4);
        ImGui::Text("Dimension");

        if (currentDim == IM_ARRAYSIZE(materialDims) - 1) // Custom
        {
            ImGui::InputText("Custom Dimension", customDim, IM_ARRAYSIZE(customDim));
            if (strlen(customDim) > 0)
                selectedDim = std::string(customDim);
        }

        if (ImGui::InputDouble("Part Length", &inputLength, 0.1, 1.0, "%.2f")) {
            inputLength = std::max(0.0, inputLength);
        }
        ImGui::InputInt("Quantity", &inputQty);

        if (ImGui::Button("Add Part")) {
            if (inputLength > 0 && inputQty > 0) {
                parts.push_back({ inputNum, inputLength, inputQty, selectedDim });
                inputLength = 0.0;
                inputQty = 0;
                inputNum[0] = '\0';
                showResults = false; // reset results when parts change
            }
        }

        ImGui::NewLine();
        ImGui::Separator();
        ImGui::NewLine();
        ImGui::Text("Parts List:");

        if (parts.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // light grey
            ImGui::Text("No parts added.");
            ImGui::PopStyleColor();
        }

        for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
            const auto&[part_number, length, quantity, dimension] = parts[i];

            ImGui::Bullet();
            ImGui::Text("%dx %.2f\" %s (%s)", quantity, length, dimension.c_str(), part_number.c_str());
            ImGui::SameLine();

            if (ImGui::SmallButton(("Delete##" + std::to_string(i)).c_str())) {
                parts.erase(parts.begin() + i);
                showResults = false;
                break;
            }
        }

        // Cluster parts by dimension & initialize default stock lengths if needed
        std::unordered_map<std::string, std::vector<Part>> partsByDimension;
        for (const auto& part : parts) {
            partsByDimension[part.dimension].push_back(part);
            if (!stockLengths.contains(part.dimension)) {
                stockLengths[part.dimension] = 288; // default stock length
            }
        }

        ImGui::NewLine();
        ImGui::Separator();
        ImGui::NewLine();
        ImGui::Text("Stock Lengths per Dimension:");

        if (parts.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // light grey
            ImGui::Text("No parts added.");
            ImGui::PopStyleColor();
        }

        for (auto& [dim, length] : stockLengths) {
            ImGui::PushID(dim.c_str());
            ImGui::InputInt(dim.c_str(), &length);
            if (length < 1) length = 1;
            ImGui::PopID();
        }

        ImGui::NewLine();
        if (ImGui::Button("Optimize")) {
            optimizationResults.clear();
            for (auto& [dim, partGroup] : partsByDimension) {
                double stockLen = stockLengths[dim];
                optimizationResults[dim].clear();
                optimizeCuts(partGroup, stockLen, optimizationResults[dim]);
            }
            showResults = true;
        }

        static bool show_pdf_popup = false;
        static std::string savedPath;

        if (showResults) {
            ImGui::SameLine();
            if (ImGui::Button("Generate PDF")) {
                std::string downloads = getDownloadsPath();
                savedPath = generateUniqueFilename(downloads, "materials_cuts", ".pdf");
                generatePDF(optimizationResults, stockLengths, parts, savedPath);
                show_pdf_popup = true;
                system(("open \"" + savedPath + "\"").c_str());
            }

            if (show_pdf_popup) {
                ImGui::OpenPopup("PDF Saved");
                show_pdf_popup = false;
            }

            if (ImGui::BeginPopupModal("PDF Saved", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("PDF has been saved to:\n%s", savedPath.c_str());
                if (ImGui::Button("OK")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Separator();
            ImGui::Text("Optimization Results Preview:");

            int totalStocksUsed = 0;
            for (auto &stocks: optimizationResults | std::views::values) {
                totalStocksUsed += static_cast<int>(stocks.size());
            }
            ImGui::Text("Total Stocks Used: %d", totalStocksUsed);

            for (auto& [dim, stocks] : optimizationResults) {
                ImGui::Text("Dimension: %s (Stock Length: %d)", dim.c_str(), stockLengths[dim]);
                for (size_t i = 0; i < stocks.size(); ++i) {
                    double used = 0.0;
                    std::string cuts = "  Stock " + std::to_string(i + 1) + ": ";
                    for (double len : stocks[i]) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.2f\" ", len);
                        cuts += buf;
                        used += len;
                    }
                    char usedBuf[64];
                    snprintf(usedBuf, sizeof(usedBuf), "(%.2f / %d)", used, stockLengths[dim]);
                    cuts += usedBuf;
                    ImGui::Text("%s", cuts.c_str());
                }
                ImGui::NewLine();
            }
        }

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}
