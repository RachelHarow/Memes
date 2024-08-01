#include <iostream>     // Standard input-output stream library
#include <fstream>      // File input-output stream library
#define CPPHTTPLIB_OPENSSL_SUPPORT  // Define to enable OpenSSL support in httplib
#include "httplib.h"    // C++ HTTP library
#include "json.hpp"     // JSON library
#define STB_IMAGE_IMPLEMENTATION  // Define to implement stb_image
#include "stb_image.h"  // stb_image library for image loading

#include "imgui.h"          // Dear ImGui library
#include "imgui_impl_dx9.h" // DirectX9 backend for ImGui
#include "imgui_impl_win32.h" // Win32 backend for ImGui
#include <d3d9.h>           // DirectX9 library
#include <tchar.h>          // Unicode/ANSI string handling
#include <unordered_map>    // Unordered map container
#include <vector>           // Vector container
#include <string>           // String handling
#include <unordered_set>    // Unordered set container
#include <mutex>            // Mutex for thread synchronization

// Data
static LPDIRECT3D9              g_pD3D = nullptr; // Direct3D9 interface
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr; // Direct3D9 device interface
static bool                     g_DeviceLost = false; // Flag to indicate if device is lost
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0; // Window resize dimensions
static D3DPRESENT_PARAMETERS    g_d3dpp = {}; // Direct3D9 presentation parameters

// Meme data
nlohmann::json meme_data; // JSON object to store meme data
std::unordered_map<std::string, LPDIRECT3DTEXTURE9> meme_textures; // Map to store meme textures
std::unordered_set<std::string> seen_images; // Set to store URLs of seen images
std::unordered_set<std::string> viewed_images; // Set to store URLs of viewed images
std::vector<std::string> generated_memes; // Vector to store URLs of generated memes
std::string fullscreen_image_url = ""; // URL of the fullscreen image
std::string create_meme_url = ""; // URL for meme creation
std::vector<std::string> text_boxes; // Vector to store text box inputs for meme creation
std::mutex meme_mutex; // Mutex for thread synchronization
std::condition_variable cv; // Condition variable for thread synchronization
bool isReady = false; // Flag to indicate if meme data is ready

// Fetch meme data from Imgflip
void FetchMemeData() {
    httplib::SSLClient client("api.imgflip.com"); // Create SSL client
    auto res = client.Get("/get_memes"); // Send GET request to fetch memes
    if (res && res->status == 200) { // Check if request was successful
        std::unique_lock<std::mutex> lock(meme_mutex); // Lock the mutex
        meme_data = nlohmann::json::parse(res->body); // Parse the response body as JSON
        isReady = true; // Set isReady flag to true
        cv.notify_all(); // Notify all waiting threads
    }
    else {
        std::cerr << "Failed to fetch meme data: " << (res ? res->status : 0) << std::endl; // Print error message
    }
}

// Load texture from memory
LPDIRECT3DTEXTURE9 LoadTextureFromMemory(unsigned char* image_data, int image_width, int image_height) {
    if (!image_data) { // Check if image data is valid
        return nullptr; // Return nullptr if invalid
    }

    LPDIRECT3DTEXTURE9 texture; // Direct3D9 texture interface
    HRESULT hr = g_pd3dDevice->CreateTexture(image_width, image_height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr); // Create texture
    if (FAILED(hr)) { // Check if texture creation failed
        return nullptr; // Return nullptr if failed
    }

    D3DLOCKED_RECT rect; // Locked rectangle for texture
    hr = texture->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD); // Lock the texture
    if (FAILED(hr)) { // Check if locking failed
        texture->Release(); // Release the texture
        return nullptr; // Return nullptr if failed
    }

    unsigned char* dst = static_cast<unsigned char*>(rect.pBits); // Pointer to destination bits
    const unsigned char* src = image_data; // Pointer to source bits

    // Copy image data to texture
    for (int y = 0; y < image_height; ++y) {
        for (int x = 0; x < image_width; ++x) {
            unsigned char r = src[4 * (y * image_width + x) + 0]; // Red component
            unsigned char g = src[4 * (y * image_width + x) + 1]; // Green component
            unsigned char b = src[4 * (y * image_width + x) + 2]; // Blue component
            unsigned char a = src[4 * (y * image_width + x) + 3]; // Alpha component
            int index = 4 * (y * image_width + x); // Source index
            int pitch_index = 4 * x + y * rect.Pitch; // Destination index

            // Check if indices are within bounds
            if (index < 0 || index >= image_width * image_height * 4) {
                texture->UnlockRect(0); // Unlock the texture
                texture->Release(); // Release the texture
                return nullptr; // Return nullptr if out of bounds
            }

            if (pitch_index < 0 || pitch_index >= rect.Pitch * image_height) {
                texture->UnlockRect(0); // Unlock the texture
                texture->Release(); // Release the texture
                return nullptr; // Return nullptr if out of bounds
            }

            dst[pitch_index + 0] = b; // Set blue component
            dst[pitch_index + 1] = g; // Set green component
            dst[pitch_index + 2] = r; // Set red component
            dst[pitch_index + 3] = a; // Set alpha component
        }
    }

    texture->UnlockRect(0); // Unlock the texture
    return texture; // Return the texture
}

// Load texture from URL
LPDIRECT3DTEXTURE9 LoadTextureFromURL(const std::string& url) {
    std::string domain = url.substr(8); // Remove 'https://'
    size_t pos = domain.find('/'); // Find position of first '/'
    std::string path = domain.substr(pos); // Extract path
    domain = domain.substr(0, pos); // Extract domain

    httplib::SSLClient client(domain.c_str()); // Create SSL client
    auto res = client.Get(path.c_str()); // Send GET request to fetch image
    if (res && res->status == 200) { // Check if request was successful
        int image_width, image_height, channels; // Image dimensions and channels
        unsigned char* image_data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(res->body.data()), res->body.size(), &image_width, &image_height, &channels, STBI_rgb_alpha); // Load image from memory
        if (image_data) { // Check if image data is valid
            LPDIRECT3DTEXTURE9 texture = LoadTextureFromMemory(image_data, image_width, image_height); // Load texture from memory
            stbi_image_free(image_data); // Free image data
            return texture; // Return the texture
        }
    }
    return nullptr; // Return nullptr if loading failed
}

// Load meme textures
void LoadMemeTextures() {
    std::unique_lock<std::mutex> lock(meme_mutex); // Lock the mutex
    cv.wait(lock, [] {return isReady; }); // Wait until meme data is ready
    for (const auto& meme : meme_data["data"]["memes"]) { // Iterate over memes
        std::string url = meme["url"].get<std::string>(); // Get meme URL
        meme_textures[url] = nullptr; // Initialize texture with nullptr
    }
}

// Get meme texture
LPDIRECT3DTEXTURE9 GetMemeTexture(const std::string& url) {
    if (meme_textures[url] == nullptr) { // Check if texture is not loaded
        meme_textures[url] = LoadTextureFromURL(url); // Load texture from URL
    }
    return meme_textures[url]; // Return the texture
}

// Compare memes for sorting
static int CompareMemes(const void* a, const void* b) {
    const nlohmann::json* memeA = static_cast<const nlohmann::json*>(a); // Pointer to first meme
    const nlohmann::json* memeB = static_cast<const nlohmann::json*>(b); // Pointer to second meme

    ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs(); // Get sort specifications
    for (int n = 0; n < sortSpecs->SpecsCount; n++) { // Iterate over sort specifications
        const ImGuiTableColumnSortSpecs* sortSpec = &sortSpecs->Specs[n]; // Get current sort specification
        int delta = 0; // Difference for sorting

        // Compare based on column index
        switch (sortSpec->ColumnIndex) {
        case 0: delta = strcmp((*memeA)["id"].get<std::string>().c_str(), (*memeB)["id"].get<std::string>().c_str()); break;
        case 1: delta = strcmp((*memeA)["name"].get<std::string>().c_str(), (*memeB)["name"].get<std::string>().c_str()); break;
        case 3: delta = (*memeA)["width"].get<int>() - (*memeB)["width"].get<int>(); break;
        case 4: delta = (*memeA)["height"].get<int>() - (*memeB)["height"].get<int>(); break;
        case 5: delta = (*memeA)["box_count"].get<int>() - (*memeB)["box_count"].get<int>(); break;
        }

        if (delta > 0) return sortSpec->SortDirection == ImGuiSortDirection_Ascending ? +1 : -1; // Ascending or descending
        if (delta < 0) return sortSpec->SortDirection == ImGuiSortDirection_Ascending ? -1 : +1; // Ascending or descending
    }

    return 0; // Return 0 if equal
}

// Save generated memes to a file
void SaveGeneratedMemes() {
    std::ofstream file("generated_memes.txt"); // Open file
    if (file.is_open()) { // Check if file is open
        for (const auto& url : generated_memes) { // Iterate over generated memes
            file << url << std::endl; // Write URL to file
        }
        file.close(); // Close file
    }
}

// Create meme with Imgflip API
std::string CreateMeme(const std::string& template_id, const std::vector<std::string>& text) {
    std::string username = "welovecpp";  // Your Imgflip username
    std::string password = "welovecpp";  // Your Imgflip password

    httplib::SSLClient client("api.imgflip.com"); // Create SSL client
    httplib::Params params; // Parameters for POST request
    params.emplace("template_id", template_id); // Add template ID to parameters
    params.emplace("username", username); // Add username to parameters
    params.emplace("password", password); // Add password to parameters

    std::cout << "Adding param: template_id = " << template_id << std::endl; // Log the template_id

    for (size_t i = 0; i < text.size(); ++i) { // Iterate over text boxes
        std::string key = "boxes[" + std::to_string(i) + "][text]"; // Key for text box
        std::string value = text[i]; // Value for text box
        std::cout << "Adding param: " << key << " = " << value << std::endl; // Log each parameter added
        params.emplace(key, value); // Add text box to parameters
    }

    auto res = client.Post("/caption_image", params); // Send POST request

    if (res) { // Check if response is valid
        if (res->status == 200) { // Check if request was successful
            auto json_response = nlohmann::json::parse(res->body); // Parse response body as JSON
            if (json_response["success"]) { // Check if response indicates success
                std::string url = json_response["data"]["url"]; // Get URL of created meme
                std::cout << "Meme created successfully: " << url << std::endl;
                generated_memes.push_back(url); // Save the URL of the generated meme
                SaveGeneratedMemes(); // Save all generated memes to file
                return url; // Return URL
            }
            else {
                std::cerr << "Imgflip API response was not successful: " << json_response.dump(4) << std::endl; // Print error message
            }
        }
        else {
            std::cerr << "Failed to create meme, HTTP status: " << res->status << std::endl; // Print error message
        }
    }
    else {
        std::cerr << "Failed to create meme, no response from server" << std::endl; // Print error message
    }

    return ""; // Return empty string if failed
}

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int main(int, char**) {
    // Fetch initial meme data
    FetchMemeData();

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Meme Generator", WS_OVERLAPPEDWINDOW, 0, 0, 1550, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard navigation
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable gamepad navigation

    // Setup Dear ImGui style
    ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Customize ImGui style
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 5.0f; // Set frame rounding
    style.GrabRounding = 5.0f; // Set grab rounding
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.8f, 0.92f, 1.0f, 1.0f); // Set window background color
    style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.8f, 0.86f, 1.0f); // Set header color
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.8f, 1.0f, 0.8f, 1.0f); // Set header hovered color
    style.Colors[ImGuiCol_Button] = ImVec4(0.53f, 0.81f, 0.98f, 1.0f); // Set button color
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.85f, 0.99f, 1.0f); // Set button hovered color

    // Load meme textures
    LoadMemeTextures();

    // Our state
    ImVec4 clear_color = ImVec4(0.89f, 0.95f, 1.00f, 1.00f); // Clear color for window
    static char search_query[200] = ""; // Search query buffer
    bool show_generated_memes = false; // Flag to show generated memes

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

        // Rendering
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

    // Cleanup
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd) {
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr) {
        return false;
    }

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice);
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

void CleanupDeviceD3D() {
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL) {
        IM_ASSERT(0);
    }
    ImGui_ImplDX9_CreateDeviceObjects();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam); // Ensure all 4 parameters are passed
}
