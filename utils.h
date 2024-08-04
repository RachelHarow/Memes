#ifndef UTILS_H
#define UTILS_H

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

extern LPDIRECT3D9 g_pD3D;
extern LPDIRECT3DDEVICE9 g_pd3dDevice;
extern bool g_DeviceLost;
extern D3DPRESENT_PARAMETERS g_d3dpp;
extern UINT g_ResizeWidth, g_ResizeHeight;

extern nlohmann::json meme_data;
extern std::unordered_map<std::string, LPDIRECT3DTEXTURE9> meme_textures;
extern std::unordered_set<std::string> seen_images;
extern std::unordered_set<std::string> viewed_images;
extern std::vector<std::string> generated_memes;
extern std::string fullscreen_image_url;
extern std::string create_meme_url;
extern std::vector<std::string> text_boxes;
extern std::mutex meme_mutex;
extern std::condition_variable cv;
extern bool isReady;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void FetchMemeData();
LPDIRECT3DTEXTURE9 LoadTextureFromMemory(unsigned char* image_data, int image_width, int image_height);
LPDIRECT3DTEXTURE9 LoadTextureFromURL(const std::string& url);
void LoadMemeTextures();
LPDIRECT3DTEXTURE9 GetMemeTexture(const std::string& url);
int CompareMemes(const void* a, const void* b);
void SaveGeneratedMemes();
std::string CreateMeme(const std::string& template_id, const std::vector<std::string>& text);
void CustomizeImGuiStyle();


#endif // UTILS_H
