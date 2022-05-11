#include "resource.h"
#include <Windows.h>

#if defined _M_IX86
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#include "dwm_symbol.h"
#include "imgui_window.h"
#include "payload.hpp"
#include "win-url-download.hpp"
#include <TlHelp32.h>
#include <atomic>
#include <thread>

#define PrintErro(text) MessageBoxA((HWND)0, text, "Erro", MB_OK | MB_TOPMOST)

void  open_binary_file(const std::string &file, std::vector<uint8_t> &data);
void  buffer_to_file_bin(unsigned char *buffer, size_t buffer_size, const std::string &filename);
DWORD FindProcess(const char *szProcess);
BOOL  EnableDebugPriv();
std::atomic<std::vector<uint8_t> *> DwmCaptureBitmap;
static ID3D11ShaderResourceView *   g_pDwmCaptureTextureView = NULL;
unsigned int                        CaptureWidth             = 0;
unsigned int                        CaptureHeight            = 0;
int WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {
    std::atomic<float> DownloadProgress;
    std::atomic<bool>  DownloadErro;

    std::string pdb_url = dwm_symbol::pdburl(dwm_symbol::GetModuleDebugInfo(dwm_symbol::module_name));

    if (!imgui_window::init()) {
        return -1;
    }

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        if (imgui_window::begin()) {

            if (g_pDwmCaptureTextureView) {
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize(imgui_window::GetGuiWindowSize(), ImGuiCond_Always);
                // Setting the padding of the window to 0 means that the picture control fills the window
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                // Set the window to be borderless
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
                // No title bar, can't stretch
                ImGui::SetNextWindowBgAlpha(0);
                static bool window = true;
                ImGui::Begin("BackGround",
                             &window,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoSavedSettings);

                ImGui::Image(g_pDwmCaptureTextureView,
                             ImVec2((float)CaptureWidth, (float)CaptureHeight),
                             ImVec2(0, 0),
                             ImVec2(1, 1),
                             ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                             ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

                ImGui::End();
                ImGui::PopStyleVar(2);
            } else {

                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize(imgui_window::GetGuiWindowSize(), ImGuiCond_Always);
                ImGui::Begin(u8"DWM.EXE Screenshot",
                             0,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize);

                static bool show_text        = false;
                static auto dwm_symbol_ready = []() -> bool {
                    for (size_t idx = 0; idx < dwm_symbol::symbol_num; idx++)
                        if (dwm_symbol::hook_offsets[idx] == 0)
                            return false;
                    return true;
                };

                if (show_text) {

                    ImGui::Text(u8"Symbol url:  %s", pdb_url.c_str());
                    ImGui::Text(u8"Download progress:  %f", DownloadProgress.load());
                    ImGui::Text(u8"Current state:  %s",
                                DownloadErro.load() ? u8"Download failed"
                                : DownloadProgress.load() == 100.f
                                    ? dwm_symbol_ready() ? u8"Offset acquisition complete" : u8"Analyzing symbols..."
                                    : u8"Downloading...");

                    for (size_t idx = 0; idx < dwm_symbol::symbol_num; idx++)
                        ImGui::Text(u8"Offset 0x%x", dwm_symbol::hook_offsets[idx]);
                } else {
                    if (ImGui::Button(u8"Click to start")) {
                        show_text = true;
                        std::thread([=, &DownloadProgress, &DownloadErro]() {
                            CBindStatusCallback *StatusCallback = CBindStatusCallback::GenerateAnInstance();
                            StatusCallback->OnProgressCallBack(
                                [=, &DownloadProgress, &DownloadErro](float Progress) { DownloadProgress = Progress; });
#if 0
                            std::vector<uint8_t> data;
                            open_binary_file(dwm_symbol::symbol_name, data);
                            if (!dwm_symbol::init(data.data())) {
                                PrintErro("Failed to fetch symbol!");
                            }
#else
                            auto hr =
                                URLDownloadToFileA(0, pdb_url.c_str(), dwm_symbol::symbol_name, 0, StatusCallback);
                            if (hr == S_OK) {
                                std::vector<uint8_t> data;
                                open_binary_file(dwm_symbol::symbol_name, data);
                                if (!dwm_symbol::init(data.data())) {
                                    PrintErro("Failed to fetch symbol!");
                                }
                            } else {
                                DownloadErro = true;
                            }
#endif

                            StatusCallback->Release();
                        }).detach();
                    }
                }

                if (dwm_symbol_ready()) {
                    static bool captureing = false;
                    if (ImGui::Button(u8"Click to take screenshot")) {
                        captureing = true;
                        std::thread([]() {
                            EnableDebugPriv();
                            auto   pid  = FindProcess("dwm.exe");
                            HANDLE hDwm = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
                            if (hDwm) {
                                for (size_t idx = 0; idx < dwm_symbol::symbol_num; idx++) {
                                    reinterpret_cast<__int64 *>(shellcode::payload +
                                                                shellcode::rva::hook_offsets)[idx] =
                                        (__int64)dwm_symbol::hook_offsets[idx];
                                }
                                LPVOID RemoteBuffer = VirtualAllocEx(hDwm,
                                                                     0,
                                                                     sizeof(shellcode::payload),
                                                                     MEM_COMMIT | MEM_RESERVE,
                                                                     PAGE_EXECUTE_READWRITE);
                                if (RemoteBuffer) {
                                    SIZE_T WriteSize = 0;
                                    WriteProcessMemory(
                                        hDwm, RemoteBuffer, shellcode::payload, sizeof(shellcode::payload), &WriteSize);
                                    SECURITY_ATTRIBUTES sec_attr{};
                                    DWORD               tid;
                                    HANDLE              hRemoteThread =
                                        CreateRemoteThread(hDwm,
                                                           &sec_attr,
                                                           NULL,
                                                           reinterpret_cast<LPTHREAD_START_ROUTINE>(
                                                               (char *)RemoteBuffer + shellcode::rva::DwmCaptureScreen),
                                                           (LPVOID)0,
                                                           NULL,
                                                           &tid);
                                    if (hRemoteThread) {
                                        WaitForSingleObject(hRemoteThread, INFINITE);
                                        DWORD ExitCode = 0;
                                        if (GetExitCodeThread(hRemoteThread, &ExitCode)) {
                                            if (ExitCode == 1) {
                                                __int64 CaptureBitmapPointer = 0;

                                                SIZE_T readSize = 0;
                                                ReadProcessMemory(hDwm,
                                                                  (LPCVOID)((DWORD64)RemoteBuffer +
                                                                            shellcode::rva::CaptureBitmapPointer),
                                                                  &CaptureBitmapPointer,
                                                                  sizeof(CaptureBitmapPointer),
                                                                  &readSize);
                                                ReadProcessMemory(
                                                    hDwm,
                                                    (LPCVOID)((DWORD64)RemoteBuffer + shellcode::rva::CaptureHeight),
                                                    &CaptureHeight,
                                                    sizeof(CaptureHeight),
                                                    &readSize);
                                                ReadProcessMemory(
                                                    hDwm,
                                                    (LPCVOID)((DWORD64)RemoteBuffer + shellcode::rva::CaptureWidth),
                                                    &CaptureWidth,
                                                    sizeof(CaptureWidth),
                                                    &readSize);

                                                std::vector<uint8_t> *bitmap = new std::vector<uint8_t>(
                                                    (size_t)CaptureWidth * (size_t)CaptureHeight * 4 +
                                                        sizeof(D3D11_TEXTURE2D_DESC),
                                                    '\0');
                                                ReadProcessMemory(hDwm,
                                                                  (LPCVOID)CaptureBitmapPointer,
                                                                  bitmap->data(),
                                                                  bitmap->size(),
                                                                  &readSize);

                                                DwmCaptureBitmap = bitmap;
                                                VirtualFreeEx(hDwm, RemoteBuffer, 0, MEM_RELEASE);
                                            }
                                            if (ExitCode == -1) {
                                                PrintErro("Failed to take screenshot!");
                                            }
                                            CloseHandle(hRemoteThread);
                                            CloseHandle(hDwm);
                                            return;
                                        } else {
                                            PrintErro("Failed to get return value!");
                                            CloseHandle(hRemoteThread);
                                            CloseHandle(hDwm);
                                            return;
                                        }
                                    } else {
                                        PrintErro("Failed to create thread!");
                                        CloseHandle(hDwm);
                                        return;
                                    }

                                } else {
                                    PrintErro("Failed to request memory!");
                                    CloseHandle(hDwm);
                                    return;
                                }

                            } else {
                                PrintErro("Failed to open process! Please relaunch as administrator.");
                                return;
                            }
                        }).detach();
                    }

                    if (captureing && !g_pDwmCaptureTextureView) {
                        static size_t count = 0;
                        count++;
                        const char *cursor = 0;

                        if (count % 3 == 0)
                            cursor = "/";
                        else if (count % 3 == 1)
                            cursor = "-";
                        else
                            cursor = "\\";

                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), u8"Intercepting, please wait... [ %s ]", cursor);
                    }
                }

                ImGui::End();
            }

            imgui_window::end();
        }

        if (!g_pDwmCaptureTextureView && DwmCaptureBitmap.load()) {
            g_pDwmCaptureTextureView =
                imgui_window::CreateDwmScreenShotShaderResourceView(DwmCaptureBitmap.load()->data());
            buffer_to_file_bin(DwmCaptureBitmap.load()->data(), DwmCaptureBitmap.load()->size(), "test.bmp");
        }
    }

    imgui_window::destroy();
    return 0;
}

// Helper functions

void open_binary_file(const std::string &file, std::vector<uint8_t> &data) {
    std::ifstream fstr(file, std::ios::binary);
    fstr.unsetf(std::ios::skipws);
    fstr.seekg(0, std::ios::end);

    const auto file_size = fstr.tellg();

    fstr.seekg(NULL, std::ios::beg);
    data.reserve(static_cast<uint32_t>(file_size));
    data.insert(data.begin(), std::istream_iterator<uint8_t>(fstr), std::istream_iterator<uint8_t>());
}

void buffer_to_file_bin(unsigned char *buffer, size_t buffer_size, const std::string &filename) {
    std::ofstream file(filename, std::ios_base::out | std::ios_base::binary);
    file.write((const char *)buffer, buffer_size);
    file.close();
}

DWORD FindProcess(const char *szProcess) {
    DWORD          dwRet     = -1;
    HANDLE         hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    Process32First(hSnapshot, &pe32);
    do {
        if (_stricmp(pe32.szExeFile, szProcess) == 0) {
            dwRet = pe32.th32ProcessID;
            break;
        }
    } while (Process32Next(hSnapshot, &pe32));
    CloseHandle(hSnapshot);
    return dwRet;
}

BOOL EnableDebugPriv() {
    HANDLE           hToken;
    LUID             sedebugnameValue;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return FALSE;
    }

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &sedebugnameValue)) {
        CloseHandle(hToken);
        return FALSE;
    }
    tkp.PrivilegeCount           = 1;
    tkp.Privileges[0].Luid       = sedebugnameValue;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL)) {
        return FALSE;
    }
    CloseHandle(hToken);
    return TRUE;
}