#include "utils.h"
static char search_query[200] = ""; // Search query buffer
bool show_generated_memes = false;
int main(int, char**) {
    FetchMemeData();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Meme Generator", WS_OVERLAPPEDWINDOW, 0, 0, 1550, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsLight();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    CustomizeImGuiStyle();

    LoadMemeTextures();

    ImVec4 clear_color = ImVec4(0.89f, 0.95f, 1.00f, 1.00f);

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle lost D3D9 device
        if (g_DeviceLost) {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST) {
                ::Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET)
                ResetDevice();
            g_DeviceLost = false;
        }

        // Handle window resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            g_d3dpp.BackBufferWidth = g_ResizeWidth;
            g_d3dpp.BackBufferHeight = g_ResizeHeight;
            g_ResizeWidth = g_ResizeHeight = 0;
            ResetDevice();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Search bar and meme data table
        if (fullscreen_image_url.empty() && create_meme_url.empty()) {
            ImGui::Begin("Meme Data Table", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Search for a meme:");
            ImGui::InputText("##Search", search_query, IM_ARRAYSIZE(search_query));
            ImGui::Spacing();
            // Add button to show all generated memes
            if (ImGui::Button("Show Generated Memes")) {
                show_generated_memes = !show_generated_memes;
            }

            if (ImGui::BeginTable("MemeTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti)) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Text Areas", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                // Handle sorting
                ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
                if (sortSpecs && sortSpecs->SpecsDirty) {
                    std::vector<nlohmann::json> memes = meme_data["data"]["memes"].get<std::vector<nlohmann::json>>();
                    qsort(&memes[0], memes.size(), sizeof(nlohmann::json), CompareMemes);
                    meme_data["data"]["memes"] = memes;
                    sortSpecs->SpecsDirty = false;
                }

                bool meme_found = false;

                for (const auto& meme : meme_data["data"]["memes"]) {
                    std::string name = meme["name"].get<std::string>();
                    if (strstr(name.c_str(), search_query) != nullptr) {
                        meme_found = true;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%s", meme["id"].get<std::string>().c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%s", name.c_str());
                        ImGui::TableNextColumn();
                        {
                            std::string url = meme["url"].get<std::string>();
                            std::string id = meme["id"].get<std::string>(); // Extract template ID
                            if (viewed_images.find(url) == viewed_images.end()) {//if the meme is not found in viewed_images
                                if (ImGui::Button(("See Image##" + meme["id"].get<std::string>()).c_str())) {
                                    seen_images.insert(url);
                                    viewed_images.insert(url);
                                }
                            }
                            else {//if i saw already the image
                                LPDIRECT3DTEXTURE9 texture = GetMemeTexture(url);
                                if (texture) {
                                    if (ImGui::ImageButton((void*)texture, ImVec2(100, 100))) {
                                        fullscreen_image_url = url;//now we can show the image in full screen if you click
                                    }
                                    if (ImGui::Button(("Create Meme##" + meme["id"].get<std::string>()).c_str())) {
                                        create_meme_url = id; // Set template ID
                                        text_boxes.clear();
                                        int box_count = meme["box_count"].get<int>();
                                        text_boxes.resize(box_count);
                                    }
                                    if (ImGui::Button(("Close Image##" + meme["id"].get<std::string>()).c_str())) {
                                        viewed_images.erase(url);
                                    }
                                }
                                else {
                                    ImGui::Text("Failed to load");
                                }
                            }
                        }
                        ImGui::TableNextColumn(); ImGui::Text("%d", meme["width"].get<int>());
                        ImGui::TableNextColumn(); ImGui::Text("%d", meme["height"].get<int>());
                        ImGui::TableNextColumn(); ImGui::Text("%d", meme["box_count"].get<int>());
                    }
                }

                if (!meme_found) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("No meme found with the name: %s", search_query);
                    ImGui::TableNextColumn(); ImGui::TableNextColumn(); ImGui::TableNextColumn(); ImGui::TableNextColumn(); ImGui::TableNextColumn();
                }

                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::End();

            if (show_generated_memes) {
                ImGui::SameLine();
                ImGui::Begin("Generated Memes", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::Text("List of all generated memes:");
                for (const auto& url : generated_memes) {
                    ImGui::Text("%s", url.c_str());
                    LPDIRECT3DTEXTURE9 texture = GetMemeTexture(url);
                    if (texture) {
                        if (ImGui::ImageButton((void*)texture, ImVec2(150, 150))) {
                            fullscreen_image_url = url; // Set the URL to display the meme in fullscreen
                        }
                    }
                }
                if (ImGui::Button("Close")) {
                    show_generated_memes = false;
                }
                ImGui::End();
            }
        }
        else if (!create_meme_url.empty()) {
            ImGui::Begin("Create Meme", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Enter text for the meme:");
            for (size_t i = 0; i < text_boxes.size(); ++i) {
                text_boxes[i].resize(200); // Ensure text_boxes[i] has enough space
                ImGui::InputText(("Text " + std::to_string(i + 1)).c_str(), &text_boxes[i][0], text_boxes[i].capacity());
            }
            if (ImGui::Button("Generate Meme")) {
                std::string meme_url = CreateMeme(create_meme_url, text_boxes);
                if (!meme_url.empty()) {
                    fullscreen_image_url = meme_url;
                }
                else {
                    std::cerr << "Failed to create meme" << std::endl;
                }
                create_meme_url.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                create_meme_url.clear();
            }
            ImGui::End();
        }

        if (!fullscreen_image_url.empty()) {
            // Display fullscreen image
            LPDIRECT3DTEXTURE9 texture = GetMemeTexture(fullscreen_image_url);
            if (texture) {
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
                ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
                ImGui::Begin("Fullscreen Image", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::Image((void*)texture, io.DisplaySize);
                if (ImGui::IsMouseClicked(0)) {
                    fullscreen_image_url = "";
                }
                ImGui::End();
            }
        }

        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST)
            g_DeviceLost = true;
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
