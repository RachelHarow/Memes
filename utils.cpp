#include "utils.h"
#include <fstream>
#include <iostream>

// Include stb_image implementation
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Include imgui_impl_win32.h
#include "imgui_impl_win32.h"
//#include <imgui_impl_win32.cpp> // This should be included elsewhere in the project

// Global variables for Direct3D9 interface
LPDIRECT3D9 g_pD3D = nullptr;
LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
bool g_DeviceLost = false; // Flag for device lost state
D3DPRESENT_PARAMETERS g_d3dpp = {};
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;

// Data structures for meme handling
nlohmann::json meme_data;
std::unordered_map<std::string, LPDIRECT3DTEXTURE9> meme_textures;
std::unordered_set<std::string> seen_images;
std::unordered_set<std::string> viewed_images;
std::vector<std::string> generated_memes;
std::string fullscreen_image_url = "";
std::string create_meme_url = "";
std::vector<std::string> text_boxes;
std::mutex meme_mutex;
std::condition_variable cv;
bool isReady = false; // Flag to indicate meme data is ready

// Function to create a Direct3D9 device
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

// Function to clean up Direct3D9 device
void CleanupDeviceD3D() {
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

// Function to reset Direct3D9 device
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

// Windows procedure function to handle various messages
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))  // Ensure this function is recognized
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Function to fetch meme data from Imgflip API
void FetchMemeData() {
    httplib::SSLClient client("api.imgflip.com");
    auto res = client.Get("/get_memes");
    if (res && res->status == 200) {
        std::unique_lock<std::mutex> lock(meme_mutex);
        meme_data = nlohmann::json::parse(res->body);
        isReady = true;
        cv.notify_all();
    }
    else {
        std::cerr << "Failed to fetch meme data: " << (res ? res->status : 0) << std::endl;
    }
}

// Function to load texture from memory
LPDIRECT3DTEXTURE9 LoadTextureFromMemory(unsigned char* image_data, int image_width, int image_height) {
    if (!image_data) {
        return nullptr;
    }

    LPDIRECT3DTEXTURE9 texture;
    HRESULT hr = g_pd3dDevice->CreateTexture(image_width, image_height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
    if (FAILED(hr)) {
        return nullptr;
    }

    D3DLOCKED_RECT rect;
    hr = texture->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD);
    if (FAILED(hr)) {
        texture->Release();
        return nullptr;
    }

    unsigned char* dst = static_cast<unsigned char*>(rect.pBits);
    const unsigned char* src = image_data;

    for (int y = 0; y < image_height; ++y) {
        for (int x = 0; x < image_width; ++x) {
            unsigned char r = src[4 * (y * image_width + x) + 0];
            unsigned char g = src[4 * (y * image_width + x) + 1];
            unsigned char b = src[4 * (y * image_width + x) + 2];
            unsigned char a = src[4 * (y * image_width + x) + 3];
            int index = 4 * (y * image_width + x);
            int pitch_index = 4 * x + y * rect.Pitch;

            if (index < 0 || index >= image_width * image_height * 4) {
                texture->UnlockRect(0);
                texture->Release();
                return nullptr;
            }

            if (pitch_index < 0 || pitch_index >= rect.Pitch * image_height) {
                texture->UnlockRect(0);
                texture->Release();
                return nullptr;
            }

            dst[pitch_index + 0] = b;
            dst[pitch_index + 1] = g;
            dst[pitch_index + 2] = r;
            dst[pitch_index + 3] = a;
        }
    }

    texture->UnlockRect(0);
    return texture;
}

// Function to load texture from a URL
LPDIRECT3DTEXTURE9 LoadTextureFromURL(const std::string& url) {
    std::string domain = url.substr(8); // Extract domain from URL
    size_t pos = domain.find('/');
    std::string path = domain.substr(pos);
    domain = domain.substr(0, pos);

    httplib::SSLClient client(domain.c_str());
    auto res = client.Get(path.c_str());
    if (res && res->status == 200) {
        int image_width, image_height, channels;
        unsigned char* image_data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(res->body.data()), res->body.size(), &image_width, &image_height, &channels, STBI_rgb_alpha);
        if (image_data) {
            LPDIRECT3DTEXTURE9 texture = LoadTextureFromMemory(image_data, image_width, image_height);
            stbi_image_free(image_data);
            return texture;
        }
    }
    return nullptr;
}

// Function to load meme textures
void LoadMemeTextures() {
    std::unique_lock<std::mutex> lock(meme_mutex);
    cv.wait(lock, [] {return isReady; });
    for (const auto& meme : meme_data["data"]["memes"]) {
        std::string url = meme["url"].get<std::string>();
        meme_textures[url] = nullptr;
    }
}

// Function to get meme texture, loading it if necessary
LPDIRECT3DTEXTURE9 GetMemeTexture(const std::string& url) {
    if (meme_textures[url] == nullptr) {
        meme_textures[url] = LoadTextureFromURL(url);
    }
    return meme_textures[url];
}

// Comparison function for sorting memes
int CompareMemes(const void* a, const void* b) {
    const nlohmann::json* memeA = static_cast<const nlohmann::json*>(a);
    const nlohmann::json* memeB = static_cast<const nlohmann::json*>(b);

    ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
    for (int n = 0; n < sortSpecs->SpecsCount; n++) {
        const ImGuiTableColumnSortSpecs* sortSpec = &sortSpecs->Specs[n];
        int delta = 0;

        switch (sortSpec->ColumnIndex) {
        case 0: delta = strcmp((*memeA)["id"].get<std::string>().c_str(), (*memeB)["id"].get<std::string>().c_str()); break;
        case 1: delta = strcmp((*memeA)["name"].get<std::string>().c_str(), (*memeB)["name"].get<std::string>().c_str()); break;
        case 3: delta = (*memeA)["width"].get<int>() - (*memeB)["width"].get<int>(); break;
        case 4: delta = (*memeA)["height"].get<int>() - (*memeB)["height"].get<int>(); break;
        case 5: delta = (*memeA)["box_count"].get<int>() - (*memeB)["box_count"].get<int>(); break;
        }

        if (delta > 0) return sortSpec->SortDirection == ImGuiSortDirection_Ascending ? +1 : -1;
        if (delta < 0) return sortSpec->SortDirection == ImGuiSortDirection_Ascending ? -1 : +1;
    }

    return 0;
}

// Function to save generated memes to a file
void SaveGeneratedMemes() {
    std::ofstream file("generated_memes.txt");
    if (file.is_open()) {
        for (const auto& url : generated_memes) {
            file << url << std::endl;
        }
        file.close();
    }
}

// Function to create a meme using Imgflip API
std::string CreateMeme(const std::string& template_id, const std::vector<std::string>& text) {
    std::string username = "welovecpp";
    std::string password = "welovecpp";

    httplib::SSLClient client("api.imgflip.com");
    httplib::Params params;
    params.emplace("template_id", template_id);
    params.emplace("username", username);
    params.emplace("password", password);

    std::cout << "Adding param: template_id = " << template_id << std::endl;

    for (size_t i = 0; i < text.size(); ++i) {
        std::string key = "boxes[" + std::to_string(i) + "][text]";
        std::string value = text[i];
        std::cout << "Adding param: " << key << " = " << value << std::endl;
        params.emplace(key, value);
    }

    auto res = client.Post("/caption_image", params);

    if (res) {
        if (res->status == 200) {
            auto json_response = nlohmann::json::parse(res->body);
            if (json_response["success"]) {
                std::string url = json_response["data"]["url"];
                std::cout << "Meme created successfully: " << url << std::endl;
                generated_memes.push_back(url);
                SaveGeneratedMemes();
                return url;
            }
            else {
                std::cerr << "Imgflip API response was not successful: " << json_response.dump(4) << std::endl;
            }
        }
        else {
            std::cerr << "Failed to create meme, HTTP status: " << res->status << std::endl;
        }
    }
    else {
        std::cerr << "Failed to create meme, no response from server" << std::endl;
    }

    return "";
}

// Function to customize ImGui style
void CustomizeImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.8f, 0.92f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.8f, 0.86f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.8f, 1.0f, 0.8f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.53f, 0.81f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.85f, 0.99f, 1.0f);
}
