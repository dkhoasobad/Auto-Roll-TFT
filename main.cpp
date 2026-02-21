
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <deque>

#include <leptonica/allheaders.h>
#include <tesseract/capi.h>
#include <tesseract/publictypes.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Forward Declarations
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void NormalizeText(const char* input, char* output);
bool StringContains(const std::string& haystack, const std::string& needle);
void FastClick(int x, int y);
void MoveMouseToCenter();
void BotThread();
void InitChampions();
PIX* CaptureSlot(int slotIndex, int* outCenterX, int* outCenterY);
PIX* CaptureShopRegion();
void CaptureSlotPreview(int slotIndex);
void UpdateSlotPreviewTexture(int slotIndex);
void UpdatePreviewTexture();

#define SCALE_FACTOR 2.0f          // Giảm từ 3x -> 2x (slot nhỏ nên 2x đủ rồi, nhanh hơn 2.25x)
#define SHOP_Y_PERCENT 0.82f
#define SHOP_H_PERCENT 0.18f

// 5 vị trí slot shop (1920x1080 baseline), tính theo % màn hình
// Mỗi slot chứa tên tướng ở phần giữa-dưới của thẻ bài
#define SHOP_SLOT_COUNT 5
struct ShopSlot {
    float x_percent;   // X center của slot (% screen width)
    float y_percent;   // Y center vùng tên tướng (% screen height)
    float w_percent;   // Width vùng crop (% screen width)
    float h_percent;   // Height vùng crop (% screen height)
};

// Giá trị mặc định cho 1920x1080 Windowed Fullscreen
static const ShopSlot SHOP_SLOTS_DEFAULT[SHOP_SLOT_COUNT] = {
    { 0.265f, 0.920f, 0.095f, 0.035f },  // Slot 1
    { 0.375f, 0.920f, 0.095f, 0.035f },  // Slot 2
    { 0.485f, 0.920f, 0.095f, 0.035f },  // Slot 3
    { 0.595f, 0.920f, 0.095f, 0.035f },  // Slot 4
    { 0.705f, 0.920f, 0.095f, 0.035f },  // Slot 5
};

// Biến chỉnh được runtime qua ImGui slider
static ShopSlot SHOP_SLOTS[SHOP_SLOT_COUNT] = {
    { 0.265f, 0.920f, 0.095f, 0.035f },
    { 0.375f, 0.920f, 0.095f, 0.035f },
    { 0.485f, 0.920f, 0.095f, 0.035f },
    { 0.595f, 0.920f, 0.095f, 0.035f },
    { 0.705f, 0.920f, 0.095f, 0.035f },
};

// Dùng chung Y và Size cho tất cả slot (vì 5 slot thường cùng hàng)
static bool g_SlotLinkY = true;      // Khi true: chỉnh Y slot 1 sẽ áp dụng cho tất cả
static bool g_SlotLinkSize = true;   // Khi true: chỉnh W/H slot 1 sẽ áp dụng cho tất cả


static ID3D11Device* g_pd3dDevice = NULL;
static ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
static IDXGISwapChain* g_pSwapChain = NULL;
static ID3D11RenderTargetView* g_mainRenderTargetView = NULL;
static ID3D11ShaderResourceView* g_PreviewTexture = NULL;

// Slot preview textures (cho tab SETTINGS)
static ID3D11ShaderResourceView* g_SlotPreviewTex[SHOP_SLOT_COUNT] = {};
struct SlotPreviewData {
    std::vector<unsigned char> pixels;
    int w = 0, h = 0;
    bool needUpdate = false;
};
static SlotPreviewData g_SlotPreview[SHOP_SLOT_COUNT];
static std::mutex g_SlotPreviewMutex;

std::mutex g_PreviewMutex;
std::vector<unsigned char> g_PreviewData;
int g_PreviewW = 0, g_PreviewH = 0;
bool g_NewPreviewAvailable = false;

std::atomic<bool> g_IsRunning(false);
std::atomic<bool> g_StopRequested(false);
std::mutex g_DataMutex;
std::string g_LastSeenText = "Waiting..."; 

struct ChampionState {
    std::string name;
    int cost;
    int star_level;
    int current_count;
    bool completed;
};

struct AppLog {
    std::vector<std::string> Logs;
    std::mutex LogMutex;
    bool AutoScroll = true;

    void AddLog(const char* fmt, ...) {
        std::lock_guard<std::mutex> lock(LogMutex);
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
        va_end(args);
        
        time_t now = time(0);
        tm* ltm = localtime(&now);
        char timeBuf[32];
        strftime(timeBuf, 32, "[%H:%M:%S] ", ltm);
        Logs.push_back(std::string(timeBuf) + std::string(buf));
        if (Logs.size() > 200) Logs.erase(Logs.begin());
    }

    void Draw(const char* title) {
        std::lock_guard<std::mutex> lock(LogMutex);
        if (ImGui::BeginChild(title, ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& line : Logs) {
                ImVec4 col = ImVec4(0.8f, 0.9f, 0.95f, 1.0f);
                // Color-code log lines by keyword
                if (line.find("MUA") != std::string::npos) col = ImVec4(0.4f, 1.0f, 0.6f, 1.0f);
                else if (line.find("ROLL") != std::string::npos) col = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
                else if (line.find("Developer") != std::string::npos) col = ImVec4(0.2f, 0.8f, 1.0f, 1.0f);
                else if (line.find("Support") != std::string::npos) col = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
                
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
};

AppLog g_Log;
std::vector<ChampionState> g_Champions;

int g_BotMode = 0;
float g_RollDelay = 0.5f;
float g_ScanDelay = 0.03f;  // Tăng tốc: giảm từ 0.1f -> 0.03f để scan nhanh hơn 3x
int g_ScanMode = 0;         // 0 = Full Scan (cũ, ổn định), 1 = Slot Scan (nhanh hơn)
bool g_DebugOCR = false;    // Log OCR text ra log để debug
char g_SearchBuffer[128] = "";
HWND g_GameWindow = NULL;

const char* CHAMPIONS_1COST[] = { 
    "Anivia", 
    "Blitzcrank", 
    "Briar", 
    "Caitlyn", 
    "Illaoi", 
    "Jarvan", 
    "Jhin", 
    "Kog'Maw", // Trong game là Kog'Maw
    "Lulu", 
    "Qiyana", 
    "Rumble", 
    "Shen", 
    "Sona", 
    "Viego", 
    NULL 
};

const char* CHAMPIONS_2COST[] = { 
    "Aphelios", 
    "Ashe", 
    "Bard", 
    "Cho'Gath", // Trong game là Cho'Gath
    "Ekko", 
    "Graves", 
    "Neeko", 
    "Orianna", 
    "Poppy", 
    "Rek'Sai", // Trong game là Rek'Sai
    "Sion", 
    "Teemo", 
    "Tristana", 
    "Tryndamere", 
    "Twisted Fate", 
    "Vi", 
    "Xin Zhao", 
    "Yasuo", 
    "Yorick", 
    NULL 
};

const char* CHAMPIONS_3COST[] = { 
    "Ahri",
    "Darius",
    "Dr. Mundo", // Hoặc "Dr Mundo" tùy vào hiển thị trong game
    "Draven",
    "Gangplank",
    "Gwen",
    "Jinx",
    "Kennen",
    "Kobuko", // Tên kép
    "LeBlanc",
    "Leona",
    "Loris",
    "Malzahar",
    "Milio",
    "Nautilus",
    "Sejuani",
    "Vayne",
    "Zoe",
    NULL 
};

const char* CHAMPIONS_4COST[] = { 
    "Ambessa",
    "Bel'Veth", // Trong ảnh là Belveth, game thường là Bel'Veth
    "Braum",
    "Diana",
    "Fizz",
    "Garen",
    "Kai'Sa",   // Trong ảnh là Kaisa, game thường là Kai'Sa
    "Kalista",
    "Lissandra",
    "Lux",
    "Miss Fortune",
    "Nasus",
    "Nidalee",
    "Renekton",
    "Sứ Giả",
    "Seraphine",
    "Singed",
    "Skarner",
    "Swain",
    "Taric",
    "Veigar",
    "Warwick",
    "Ngộ Không",
    "Yone",
    "Yunara",
    NULL 
};

const char* CHAMPIONS_5COST[] = { 
    "Aatrox",
    "Sylas", // Lưu ý: Amumu thường là 1 tiền, nhưng nếu ảnh này đúng thì để ở đây
    "Annie",
    "Azir",
    "Fiddlesticks",
    "Galio",
    "Kindred",
    "Lucian", // Tên kép
    "Mel",
    "Ornn",
    "Sett",
    "Shyvana",
    "T-Hex",
    "Tahm Kench",
    "Thresh",
    "Zaahen",
    "Volibear",
    "Xerath",
    "Ziggs",
    "Zilean",
    "Baron",
    NULL 
};

void InitChampions() {
    auto add_list = [](const char** list, int cost) {
        for (int i = 0; list[i]; i++) {
            ChampionState c;
            c.name = list[i];
            c.cost = cost;
            c.star_level = 0;
            c.current_count = 0;
            c.completed = false;
            g_Champions.push_back(c);
        }
    };
    add_list(CHAMPIONS_1COST, 1);
    add_list(CHAMPIONS_2COST, 2);
    add_list(CHAMPIONS_3COST, 3);
    add_list(CHAMPIONS_4COST, 4);
    add_list(CHAMPIONS_5COST, 5);
}

// --- UTILITY IMPLEMENTATION ---
void NormalizeText(const char* input, char* output) {
    int j = 0;
    for (int i = 0; input[i]; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c == ' ' || c == '.' || c == '-' || c == '&' || c == '\'' || c == '\n' || c == '\r') continue; 
        if (c > 127) { output[j++] = c; continue; }
        if (std::isalnum(c)) {
            char low = std::tolower(c);
            if (low == '1' || low == 'l') low = 'i'; 
            output[j++] = low;
        }
    }
    output[j] = '\0';
}

bool StringContains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    char hNorm[256], nNorm[256];
    NormalizeText(haystack.c_str(), hNorm);
    NormalizeText(needle.c_str(), nNorm);
    return strstr(hNorm, nNorm) != NULL;
}

void MoveMouseToCenter() {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    SetCursorPos(screenW / 2, screenH / 2);
}

// --- CLICK LOGIC ---
void FastClick(int x, int y) {
    SetCursorPos(x, y);
    Sleep(30);  // Chờ chuột đến đúng vị trí

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE; inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE; inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

    SendInput(2, inputs, sizeof(INPUT)); // Click trái 1 lần duy nhất

    Sleep(30);
    MoveMouseToCenter();
}

// Capture 1 slot riêng biệt → PIX nhỏ, OCR cực nhanh
PIX* CaptureSlot(int slotIndex, int* outCenterX, int* outCenterY) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    const ShopSlot& slot = SHOP_SLOTS[slotIndex];
    int cx = (int)(screenW * slot.x_percent);
    int cy = (int)(screenH * slot.y_percent);
    int w  = (int)(screenW * slot.w_percent);
    int h  = (int)(screenH * slot.h_percent);
    int x  = cx - w / 2;
    int y  = cy - h / 2;

    // Clamp
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > screenW) w = screenW - x;
    if (y + h > screenH) h = screenH - y;

    *outCenterX = cx;
    *outCenterY = cy;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    int dataSize = w * h * 4;
    BYTE* pixels = (BYTE*)malloc(dataSize);
    GetDIBits(hdcMem, hbm, 0, h, pixels, &bmi, DIB_RGB_COLORS);

    PIX* pix = pixCreate(w, h, 8);
    pixSetXRes(pix, 300);
    pixSetYRes(pix, 300);
    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            int idx = (iy * w + ix) * 4;
            BYTE b = pixels[idx]; BYTE g = pixels[idx + 1]; BYTE r = pixels[idx + 2];
            BYTE gray = (BYTE)(0.299f * r + 0.587f * g + 0.114f * b);
            pixSetPixel(pix, ix, iy, gray);
        }
    }

    free(pixels);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return pix;
}

// Capture toàn bộ shop — dùng cho cả preview và Full Scan mode
PIX* CaptureShopRegion() {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = 0; int y = (int)(screenH * SHOP_Y_PERCENT);
    int w = screenW; int h = (int)(screenH * SHOP_H_PERCENT);

    HDC hdcScreen = GetDC(NULL); HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hbm); BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);

    BITMAPINFO bmi = {0}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;

    int dataSize = w * h * 4; BYTE* pixels = (BYTE*)malloc(dataSize);
    GetDIBits(hdcMem, hbm, 0, h, pixels, &bmi, DIB_RGB_COLORS);

    { std::lock_guard<std::mutex> lock(g_PreviewMutex);
      g_PreviewW = w; g_PreviewH = h;
      g_PreviewData.assign(pixels, pixels + dataSize);
      g_NewPreviewAvailable = true; }

    PIX* pix = pixCreate(w, h, 8); pixSetXRes(pix, 300); pixSetYRes(pix, 300);
    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            int idx = (iy * w + ix) * 4;
            BYTE b = pixels[idx]; BYTE g = pixels[idx+1]; BYTE r = pixels[idx+2];
            BYTE gray = (BYTE)(0.299f*r + 0.587f*g + 0.114f*b);
            pixSetPixel(pix, ix, iy, gray);
        }
    }
    free(pixels); DeleteObject(hbm); DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
    return pix;
}

void UpdatePreviewTexture() {
    std::lock_guard<std::mutex> lock(g_PreviewMutex);
    if (!g_NewPreviewAvailable) return;
    if (g_PreviewTexture) { g_PreviewTexture->Release(); g_PreviewTexture = NULL; }
    if (g_PreviewData.empty() || g_PreviewW <= 0 || g_PreviewH <= 0) return;

    D3D11_TEXTURE2D_DESC desc = {}; 
    desc.Width = g_PreviewW; desc.Height = g_PreviewH; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = g_PreviewData.data();
    subResource.SysMemPitch = desc.Width * 4;

    ID3D11Texture2D* pTexture = NULL;
    g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    if (pTexture) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format; srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0; srvDesc.Texture2D.MipLevels = 1;
        g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &g_PreviewTexture);
        pTexture->Release();
    }
    g_NewPreviewAvailable = false;
}

// Capture 1 slot ra BGRA pixels cho preview trong tab SETTINGS
void CaptureSlotPreview(int slotIndex) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    const ShopSlot& slot = SHOP_SLOTS[slotIndex];
    int cx = (int)(screenW * slot.x_percent);
    int cy = (int)(screenH * slot.y_percent);
    int w  = (int)(screenW * slot.w_percent);
    int h  = (int)(screenH * slot.h_percent);
    int x  = cx - w / 2;
    int y  = cy - h / 2;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x + w > screenW) w = screenW - x;
    if (y + h > screenH) h = screenH - y;
    if (w <= 0 || h <= 0) return;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    int dataSize = w * h * 4;
    BYTE* pixels = (BYTE*)malloc(dataSize);
    GetDIBits(hdcMem, hbm, 0, h, pixels, &bmi, DIB_RGB_COLORS);

    {
        std::lock_guard<std::mutex> lock(g_SlotPreviewMutex);
        g_SlotPreview[slotIndex].w = w;
        g_SlotPreview[slotIndex].h = h;
        g_SlotPreview[slotIndex].pixels.assign(pixels, pixels + dataSize);
        g_SlotPreview[slotIndex].needUpdate = true;
    }

    free(pixels); DeleteObject(hbm); DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
}

// Update slot preview texture cho D3D11 (gọi từ render thread)
void UpdateSlotPreviewTexture(int slotIndex) {
    std::lock_guard<std::mutex> lock(g_SlotPreviewMutex);
    SlotPreviewData& sp = g_SlotPreview[slotIndex];
    if (!sp.needUpdate) return;
    if (g_SlotPreviewTex[slotIndex]) { g_SlotPreviewTex[slotIndex]->Release(); g_SlotPreviewTex[slotIndex] = NULL; }
    if (sp.pixels.empty() || sp.w <= 0 || sp.h <= 0) return;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = sp.w; desc.Height = sp.h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = sp.pixels.data();
    sub.SysMemPitch = sp.w * 4;

    ID3D11Texture2D* pTex = NULL;
    g_pd3dDevice->CreateTexture2D(&desc, &sub, &pTex);
    if (pTex) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format; srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0; srvDesc.Texture2D.MipLevels = 1;
        g_pd3dDevice->CreateShaderResourceView(pTex, &srvDesc, &g_SlotPreviewTex[slotIndex]);
        pTex->Release();
    }
    sp.needUpdate = false;
}

// --- BOT LOGIC ---

// Helper: match OCR text với champion list, trả về tên tướng match hoặc ""
// Nếu match → cập nhật count và log
std::string TryMatchChampion(const char* ocrNorm, int slotOrLine, int clickX, int clickY) {
    std::lock_guard<std::mutex> lock(g_DataMutex);
    for (auto& champ : g_Champions) {
        if (champ.star_level == 0 || champ.completed) continue;

        char champNorm[100];
        NormalizeText(champ.name.c_str(), champNorm);

        bool match = false;
        // Full match
        if (strstr(ocrNorm, champNorm) != NULL) {
            match = true;
        }
        // Partial match: ít nhất 4 ký tự đầu khớp
        else if (strlen(champNorm) >= 4) {
            char partial[100];
            strncpy_s(partial, champNorm, 4);
            partial[4] = '\0';
            if (strstr(ocrNorm, partial) != NULL) match = true;
        }
        // Fallback cho tên ngắn (Vi, Mel, Zoe...): full match tên ngắn
        else if (strlen(champNorm) >= 2 && strlen(champNorm) < 4) {
            if (strstr(ocrNorm, champNorm) != NULL) match = true;
        }

        if (match) {
            champ.current_count++;
            int req = 0;
            if (champ.star_level == 2) req = 3;
            else if (champ.star_level == 3) req = 9;
            else if (champ.star_level == 99) req = 99999;

            if (champ.star_level != 99 && champ.current_count >= req) {
                champ.completed = true;
            }

            if (champ.star_level == 99)
                g_Log.AddLog("MUA: %s [%d] (INF/%d)", champ.name.c_str(), slotOrLine, champ.current_count);
            else
                g_Log.AddLog("MUA: %s [%d] (%d/%d)", champ.name.c_str(), slotOrLine, champ.current_count, req);
            return champ.name;
        }
    }
    return "";
}

// ===== FULL SCAN MODE: quét toàn bộ shop region (ổn định, đã chứng minh hoạt động) =====
void BotThread_FullScan(TessBaseAPI* api) {
    bool forceRescan = false;

    while (!g_StopRequested) {
        if (!g_IsRunning) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
        if (!forceRescan) std::this_thread::sleep_for(std::chrono::milliseconds((int)(g_ScanDelay * 1000)));
        forceRescan = false;

        // Nếu user chuyển sang Slot mode → thoát loop này
        if (g_ScanMode != 0) return;

        PIX* raw = CaptureShopRegion();
        if (!raw) continue;

        PIX* scaled = pixScale(raw, SCALE_FACTOR, SCALE_FACTOR);
        PIX* thresh = pixThresholdToBinary(scaled, 170);

        TessBaseAPISetImage2(api, thresh);
        TessBaseAPIRecognize(api, NULL);

        TessResultIterator* iter = TessBaseAPIGetIterator(api);
        TessPageIterator* pageIter = TessResultIteratorGetPageIterator(iter);

        bool foundSomething = false;
        std::string debugText = "";
        int lineIdx = 0;

        if (iter) {
            do {
                char* lineText = TessResultIteratorGetUTF8Text(iter, tesseract::RIL_TEXTLINE);
                if (!lineText) continue;
                if (strlen(lineText) < 1) { TessDeleteText(lineText); continue; }

                lineIdx++;
                char ocrNorm[256];
                NormalizeText(lineText, ocrNorm);
                debugText += std::string(lineText) + " ";

                if (g_DebugOCR) {
                    g_Log.AddLog("OCR[%d]: \"%s\" -> \"%s\"", lineIdx, lineText, ocrNorm);
                }

                // Tính vị trí click từ bounding box
                int left, top, right, bottom;
                TessPageIteratorBoundingBox(pageIter, tesseract::RIL_TEXTLINE, &left, &top, &right, &bottom);

                int screenH = GetSystemMetrics(SM_CYSCREEN);
                int screenW = GetSystemMetrics(SM_CXSCREEN);
                int shopY = (int)(screenH * SHOP_Y_PERCENT);
                int realX = (int)((left + (right - left) / 2) / SCALE_FACTOR);
                int realY = (int)((top + (bottom - top) / 2) / SCALE_FACTOR + shopY);

                if (realX > screenW * 0.84f) { TessDeleteText(lineText); continue; }

                std::string matched = TryMatchChampion(ocrNorm, lineIdx, realX, realY);
                if (!matched.empty()) {
                    FastClick(realX, realY);
                    foundSomething = true;
                }

                TessDeleteText(lineText);
            } while (TessPageIteratorNext(pageIter, tesseract::RIL_TEXTLINE));
            TessResultIteratorDelete(iter);
        }

        g_LastSeenText = debugText.empty() ? "Scanning..." : debugText;
        pixDestroy(&raw); pixDestroy(&scaled); pixDestroy(&thresh);

        if (foundSomething) {
            forceRescan = true;
        }
    }
}

// ===== SLOT SCAN MODE: quét 5 slot riêng biệt song song (nhanh hơn) =====
void BotThread_SlotScan(TessBaseAPI* slotApi[SHOP_SLOT_COUNT]) {
    int previewCounter = 0;

    while (!g_StopRequested) {
        if (!g_IsRunning) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

        // Nếu user chuyển sang Full mode → thoát loop này
        if (g_ScanMode != 1) return;

        // Update preview mỗi 10 vòng
        previewCounter++;
        if (previewCounter >= 10) {
            CaptureShopRegion();
            previewCounter = 0;
        }

        std::string debugText = "";
        bool foundSomething = false;

        struct SlotResult {
            bool matched;
            int centerX, centerY;
            std::string champName;
            std::string ocrText;
        };
        SlotResult results[SHOP_SLOT_COUNT];
        std::thread slotThreads[SHOP_SLOT_COUNT];

        for (int s = 0; s < SHOP_SLOT_COUNT; s++) {
            results[s].matched = false;

            slotThreads[s] = std::thread([&, s]() {
                int cx, cy;
                PIX* raw = CaptureSlot(s, &cx, &cy);
                if (!raw) return;

                PIX* scaled = pixScale(raw, SCALE_FACTOR, SCALE_FACTOR);
                PIX* thresh = pixThresholdToBinary(scaled, 170);

                TessBaseAPISetImage2(slotApi[s], thresh);
                TessBaseAPIRecognize(slotApi[s], NULL);

                char* text = TessBaseAPIGetUTF8Text(slotApi[s]);
                if (text && strlen(text) >= 1) {
                    char ocrNorm[256];
                    NormalizeText(text, ocrNorm);
                    results[s].ocrText = text;

                    if (g_DebugOCR) {
                        g_Log.AddLog("OCR[Slot%d]: \"%s\" -> \"%s\"", s + 1, text, ocrNorm);
                    }

                    std::string matched = TryMatchChampion(ocrNorm, s + 1, cx, cy);
                    if (!matched.empty()) {
                        results[s].matched = true;
                        results[s].centerX = cx;
                        results[s].centerY = cy;
                        results[s].champName = matched;
                    }
                }
                if (text) TessDeleteText(text);
                pixDestroy(&raw);
                pixDestroy(&scaled);
                pixDestroy(&thresh);
            });
        }

        for (int s = 0; s < SHOP_SLOT_COUNT; s++) {
            if (slotThreads[s].joinable()) slotThreads[s].join();
        }

        for (int s = 0; s < SHOP_SLOT_COUNT; s++) {
            if (results[s].matched) {
                FastClick(results[s].centerX, results[s].centerY);
                foundSomething = true;
                Sleep(50);
            }
        }

        for (int s = 0; s < SHOP_SLOT_COUNT; s++) {
            if (!results[s].ocrText.empty()) {
                debugText += "[" + std::to_string(s + 1) + "]" + results[s].ocrText + " ";
            }
        }
        g_LastSeenText = debugText.empty() ? "Scanning..." : debugText;

        if (!foundSomething) {
            std::this_thread::sleep_for(std::chrono::milliseconds((int)(g_ScanDelay * 1000)));
        }
    }
}

void BotThread() {
    // Tạo 1 API cho Full Scan mode
    TessBaseAPI* fullApi = TessBaseAPICreate();
    if (TessBaseAPIInit3(fullApi, NULL, "vie") != 0) {
        g_Log.AddLog("ERROR: Init Tesseract (Full) Failed!"); return;
    }
    TessBaseAPISetVariable(fullApi, "debug_file", "/dev/null");
    TessBaseAPISetVariable(fullApi, "tessedit_char_whitelist", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ' ");
    TessBaseAPISetVariable(fullApi, "classify_bln_numeric_mode", "1");
    TessBaseAPISetPageSegMode(fullApi, tesseract::PSM_SPARSE_TEXT);

    // Tạo 5 API cho Slot Scan mode
    TessBaseAPI* slotApi[SHOP_SLOT_COUNT];
    for (int i = 0; i < SHOP_SLOT_COUNT; i++) {
        slotApi[i] = TessBaseAPICreate();
        if (TessBaseAPIInit3(slotApi[i], NULL, "vie") != 0) {
            g_Log.AddLog("ERROR: Init Tesseract Slot %d Failed!", i); return;
        }
        TessBaseAPISetVariable(slotApi[i], "debug_file", "/dev/null");
        TessBaseAPISetVariable(slotApi[i], "tessedit_char_whitelist", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ' ");
        TessBaseAPISetVariable(slotApi[i], "classify_bln_numeric_mode", "1");
        TessBaseAPISetPageSegMode(slotApi[i], tesseract::PSM_SINGLE_LINE);
    }

    g_Log.AddLog("------------------------------------------------------");
    g_Log.AddLog("Success! Dual-Mode Detection Active.");
    g_Log.AddLog("Developer: @userdodangkhoa x @xuancuong2006");
    g_Log.AddLog("Support: 1920x1080 ( Windowed Fullscreen )");
    g_Log.AddLog("------------------------------------------------------");

    // Main loop: chuyển đổi giữa 2 mode
    while (!g_StopRequested) {
        if (g_ScanMode == 0) {
            g_Log.AddLog("Mode: Full Scan (On dinh)");
            BotThread_FullScan(fullApi);
        } else {
            g_Log.AddLog("Mode: Slot Scan (Nhanh)");
            BotThread_SlotScan(slotApi);
        }
        // Chờ một chút khi chuyển mode
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    TessBaseAPIEnd(fullApi); TessBaseAPIDelete(fullApi);
    for (int i = 0; i < SHOP_SLOT_COUNT; i++) {
        TessBaseAPIEnd(slotApi[i]); TessBaseAPIDelete(slotApi[i]);
    }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetProcessDPIAware();

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "TFTBotClassFinal";

    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, "TFT Bot - Auto Purchase", WS_OVERLAPPEDWINDOW, 100, 100, 1150, 750, NULL, NULL, hInstance, NULL);
    
    // Tìm game window để gửi phím
    g_GameWindow = FindWindowA(NULL, "Teamfight Tactics");
    if (!g_GameWindow) g_GameWindow = FindWindowA(NULL, "League of Legends");
    if (!g_GameWindow) g_GameWindow = GetForegroundWindow();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2; sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, NULL, &g_pd3dDeviceContext);
    ID3D11Texture2D* pBackBuffer; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView); pBackBuffer->Release();

    ShowWindow(hwnd, SW_SHOWDEFAULT); UpdateWindow(hwnd);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO();
    ImFontConfig fc; fc.MergeMode = false;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f, &fc, io.Fonts->GetGlyphRangesVietnamese());
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f);
    
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f; style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool botInitDone = false;
    bool done = false;

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        {
            if (!botInitDone) {
                InitChampions();
                std::thread logicThread(BotThread); logicThread.detach();
                botInitDone = true;
                g_Log.AddLog("Bot Started.");
                g_Log.AddLog("------------------------------------------------------");
            }
            if (GetAsyncKeyState(VK_F1) & 0x8000) g_IsRunning = false;
            if (GetAsyncKeyState(VK_F2) & 0x8000) g_IsRunning = true;

            ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Main", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
            
            if (ImGui::BeginTabBar("Tabs")) {
                if (ImGui::BeginTabItem(" CONTROL ")) {
                    ImGui::Spacing();
                    ImGui::BeginChild("Ctrl", ImVec2(250, 0), true);
                    ImGui::TextColored(ImVec4(0.2f,0.7f,0.65f,1), "%s", "SYSTEM"); ImGui::Separator();
                    ImGui::SliderFloat("Scan", &g_ScanDelay, 0.01f, 1.0f);

                    // Scan Mode toggle
                    const char* modeNames[] = { "Full Scan", "Slot Scan" };
                    ImGui::Combo("Mode", &g_ScanMode, modeNames, 2);
                    if (g_ScanMode == 0)
                        ImGui::TextDisabled("On dinh, quet toan bo shop");
                    else
                        ImGui::TextDisabled("Nhanh hon, can chinh slot");

                    // Debug OCR toggle
                    ImGui::Checkbox("Debug OCR", &g_DebugOCR);
                    if (g_DebugOCR)
                        ImGui::TextDisabled("Log tat ca OCR text ra Log");

                    ImGui::Separator(); ImGui::TextDisabled("OCR: %s", g_LastSeenText.substr(0,50).c_str());
                    ImGui::Separator();

                    if(g_IsRunning) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f,0.3f,0.3f,1)); if(ImGui::Button("STOP [F1]", ImVec2(-1,40))) g_IsRunning=false; ImGui::PopStyleColor(); }
                    else { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f,0.7f,0.4f,1)); if(ImGui::Button("START [F2]", ImVec2(-1,40))) g_IsRunning=true; ImGui::PopStyleColor(); }
                    if(ImGui::Button("Reset All", ImVec2(-1,30))) { std::lock_guard<std::mutex> l(g_DataMutex); for(auto& c:g_Champions){c.current_count=0;c.completed=false;}}
                    ImGui::EndChild();
                    ImGui::SameLine();
                    ImGui::BeginChild("Log", ImVec2(0,0), true); g_Log.Draw("Logs"); ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(" UNITS ")) {
                    ImGui::BeginChild("List", ImVec2(0,0), true);
                    ImGui::InputText("Search", g_SearchBuffer, 128); ImGui::Separator();
                    for(int i=1; i<=5; i++) {
                        std::vector<ChampionState*> v;
                        { std::lock_guard<std::mutex> l(g_DataMutex); for(auto&c:g_Champions) if(c.cost==i && (strlen(g_SearchBuffer)==0 || StringContains(c.name, g_SearchBuffer))) v.push_back(&c); }
                        if(v.empty()) continue;
                        ImGui::TextColored(ImVec4(1,0.8f,0.2f,1), "COST %d", i);
                        int cols=5; float w=(ImGui::GetContentRegionAvail().x-(cols-1)*8)/cols;
                        for(int k=0; k<v.size(); k++) {
                            if(k%cols!=0) ImGui::SameLine();
                            ImVec4 c;
                            if (v[k]->star_level == 2) c = ImVec4(0.9f, 0.8f, 0.3f, 1.0f);
                            else if (v[k]->star_level == 3) c = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                            else if (v[k]->star_level == 99) c = ImVec4(0.2f, 0.7f, 1.0f, 1.0f);
                            else c = ImVec4(0.2f, 0.25f, 0.3f, 1.0f);

                            if(v[k]->completed) c=ImVec4(0.3f,0.8f,0.4f,1);
                            
                            ImGui::PushStyleColor(ImGuiCol_Button, c);
                            char l[64]; 
                            if (v[k]->star_level == 99) snprintf(l,64,"%s\n(INF)", v[k]->name.c_str());
                            else if (v[k]->star_level > 0) snprintf(l,64,"%s\n(%d)", v[k]->name.c_str(), v[k]->current_count);
                            else snprintf(l,64,"%s", v[k]->name.c_str());

                            if(ImGui::Button(l, ImVec2(w,45))) { 
                                if(v[k]->star_level==0) v[k]->star_level=2; 
                                else if(v[k]->star_level==2) v[k]->star_level=3;
                                else if(v[k]->star_level==3) v[k]->star_level=99;
                                else { v[k]->star_level=0; v[k]->current_count=0; v[k]->completed=false; }
                            }
                            ImGui::PopStyleColor();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(" PREVIEW ")) {
                    UpdatePreviewTexture();
                    if(g_PreviewTexture) {
                        float ar = (float)g_PreviewH/g_PreviewW;
                        ImGui::Image((void*)g_PreviewTexture, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x*ar));
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(" SETTINGS ")) {
                    ImGui::BeginChild("SettingsChild", ImVec2(0, 0), true);

                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "SHOP SLOT POSITIONS");
                    ImGui::TextDisabled("Tinh theo %% man hinh (0.0 ~ 1.0). X=vi tri ngang, Y=vi tri doc");
                    ImGui::Separator();

                    // Checkbox link mode
                    ImGui::Checkbox("Dong bo Y (tat ca slot cung hang)", &g_SlotLinkY);
                    ImGui::SameLine();
                    ImGui::Checkbox("Dong bo Size", &g_SlotLinkSize);
                    ImGui::Separator();

                    int screenW = GetSystemMetrics(SM_CXSCREEN);
                    int screenH = GetSystemMetrics(SM_CYSCREEN);
                    ImGui::TextDisabled("Man hinh: %dx%d", screenW, screenH);

                    // Nút capture tất cả slot 1 lần
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
                    if (ImGui::Button("Capture All Slots", ImVec2(0, 0))) {
                        for (int s = 0; s < SHOP_SLOT_COUNT; s++)
                            CaptureSlotPreview(s);
                    }
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    for (int s = 0; s < SHOP_SLOT_COUNT; s++) {
                        ImGui::PushID(s);

                        char slotLabel[32];
                        snprintf(slotLabel, 32, "Slot %d", s + 1);

                        int px = (int)(screenW * SHOP_SLOTS[s].x_percent);
                        int py = (int)(screenH * SHOP_SLOTS[s].y_percent);
                        int pw = (int)(screenW * SHOP_SLOTS[s].w_percent);
                        int ph = (int)(screenH * SHOP_SLOTS[s].h_percent);

                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s  [%d, %d] %dx%d px", slotLabel, px, py, pw, ph);

                        // Nút capture slot riêng lẻ
                        ImGui::SameLine();
                        char capBtn[32]; snprintf(capBtn, 32, "Capture##%d", s);
                        if (ImGui::SmallButton(capBtn)) {
                            CaptureSlotPreview(s);
                        }

                        // X - luôn chỉnh riêng
                        char xLabel[16]; snprintf(xLabel, 16, "X##%d", s);
                        ImGui::SliderFloat(xLabel, &SHOP_SLOTS[s].x_percent, 0.0f, 1.0f, "%.3f");

                        // Y - link mode
                        if (!g_SlotLinkY || s == 0) {
                            char yLabel[16]; snprintf(yLabel, 16, "Y##%d", s);
                            if (ImGui::SliderFloat(yLabel, &SHOP_SLOTS[s].y_percent, 0.0f, 1.0f, "%.3f")) {
                                if (g_SlotLinkY) {
                                    for (int j = 1; j < SHOP_SLOT_COUNT; j++)
                                        SHOP_SLOTS[j].y_percent = SHOP_SLOTS[0].y_percent;
                                }
                            }
                        }

                        // W/H - link mode
                        if (!g_SlotLinkSize || s == 0) {
                            char wLabel[16]; snprintf(wLabel, 16, "W##%d", s);
                            char hLabel[16]; snprintf(hLabel, 16, "H##%d", s);
                            if (ImGui::SliderFloat(wLabel, &SHOP_SLOTS[s].w_percent, 0.01f, 0.3f, "%.3f")) {
                                if (g_SlotLinkSize) {
                                    for (int j = 1; j < SHOP_SLOT_COUNT; j++)
                                        SHOP_SLOTS[j].w_percent = SHOP_SLOTS[0].w_percent;
                                }
                            }
                            if (ImGui::SliderFloat(hLabel, &SHOP_SLOTS[s].h_percent, 0.01f, 0.2f, "%.3f")) {
                                if (g_SlotLinkSize) {
                                    for (int j = 1; j < SHOP_SLOT_COUNT; j++)
                                        SHOP_SLOTS[j].h_percent = SHOP_SLOTS[0].h_percent;
                                }
                            }
                        }

                        // === SLOT PREVIEW IMAGE ===
                        UpdateSlotPreviewTexture(s);
                        if (g_SlotPreviewTex[s]) {
                            float imgW = ImGui::GetContentRegionAvail().x;
                            float aspect = 0.0f;
                            {
                                std::lock_guard<std::mutex> lock(g_SlotPreviewMutex);
                                if (g_SlotPreview[s].w > 0 && g_SlotPreview[s].h > 0)
                                    aspect = (float)g_SlotPreview[s].h / (float)g_SlotPreview[s].w;
                            }
                            if (aspect > 0.0f) {
                                float imgH = imgW * aspect;
                                if (imgH < 30.0f) imgH = 30.0f; // Chiều cao tối thiểu
                                if (imgH > 80.0f) imgH = 80.0f; // Không quá cao
                                ImGui::Image((void*)g_SlotPreviewTex[s], ImVec2(imgW, imgH));
                            }
                        } else {
                            ImGui::TextDisabled("  (Nhan Capture de xem preview)");
                        }

                        if (s < SHOP_SLOT_COUNT - 1) ImGui::Separator();
                        ImGui::PopID();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();

                    // Nút Reset Default
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
                    if (ImGui::Button("Reset Default", ImVec2(-1, 35))) {
                        for (int s = 0; s < SHOP_SLOT_COUNT; s++)
                            SHOP_SLOTS[s] = SHOP_SLOTS_DEFAULT[s];
                        g_Log.AddLog("Slot positions reset to default.");
                    }
                    ImGui::PopStyleColor();

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

        ImGui::Render(); float cc[4] = {0.08f, 0.10f, 0.12f, 1};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    if(g_pSwapChain) g_pSwapChain->Release(); if(g_pd3dDevice) g_pd3dDevice->Release();
    return 0;
}