#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <set>
#include <map>
#include <chrono>
#include "resource.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1

class KeyboardSoundApp {
private:
    HHOOK hookHandle;
    std::queue<DWORD> keyQueue;
    std::mutex queueMutex;
    bool running;
    std::wstring soundPath;
    std::thread audioThread;
    HWND hwnd;
    NOTIFYICONDATAW nid;
    std::set<DWORD> pressedKeys;
    std::chrono::steady_clock::time_point lastSoundTime;
    static const int DEBOUNCE_MS = 80;
    HANDLE hMutex; 

    std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }

    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION) {
            if (wParam == WM_KEYDOWN) {
                KBDLLHOOKSTRUCT* pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
                

                if (g_pApp) {
                    g_pApp->onKeyPressed(pKeyboard->vkCode);
                }
            }
            else if (wParam == WM_KEYUP) {
                KBDLLHOOKSTRUCT* pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
                if (g_pApp) {
                    g_pApp->onKeyReleased(pKeyboard->vkCode);
                }
            }
        }
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_TRAYICON) {
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, L"Exit");
                
                SetForegroundWindow(hwnd);
                UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                
                if (cmd == 1) {
                    if (g_pApp) {
                        g_pApp->Stop();
                        PostQuitMessage(0);
                    }
                }
            }
            return 0;
        }
        
        if (msg == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    void audioWorkerThread() {
        while (running) {
            DWORD vkCode = 0;
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (!keyQueue.empty()) {
                    vkCode = keyQueue.front();
                    keyQueue.pop();
                }
            }

            if (vkCode != 0) {
                playSound();
            }
            else {
                Sleep(10);
            }
        }
    }

    void playSound() {
        PlaySoundW(soundPath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
    }

public:
    static KeyboardSoundApp* g_pApp;

    KeyboardSoundApp(const std::wstring& sound) 
        : hookHandle(NULL), running(false), soundPath(sound), hwnd(NULL), hMutex(NULL) {
        g_pApp = this;
        
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.lpszClassName = L"KeyboardSoundApp";
        RegisterClassW(&wc);
        
        hwnd = CreateWindowExW(0, L"KeyboardSoundApp", L"Keyboard Sound", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    }

    ~KeyboardSoundApp() {
        Stop();
    }

    bool Start() {
        hMutex = CreateMutexW(NULL, TRUE, L"KeyboardSoundAppMutex");
        if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (hMutex) {
                CloseHandle(hMutex);
                hMutex = NULL;
            }
            MessageBoxW(NULL, L"Dance In The Duck is already running!", L"Application Error", MB_OK | MB_ICONWARNING);
            return false;
        }

        running = true;
        hookHandle = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
        
        if (!hookHandle) {
            running = false;
            if (hMutex) {
                ReleaseMutex(hMutex);
                CloseHandle(hMutex);
                hMutex = NULL;
            }
            return false;
        }

        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = TRAY_ICON_ID;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash);
        }
        
        std::string iconPath = exeDir + "\\icon.ico";
        HANDLE iconHandle = NULL;
        
        if (GetFileAttributesA(iconPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            lastSlash = exeDir.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                iconPath = exeDir.substr(0, lastSlash) + "\\icon.ico";
            }
        }
        
        nid.hIcon = (HICON)LoadImageA(NULL, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        if (!nid.hIcon) {
            nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        }
        
        wcscpy_s(nid.szTip, 128, L"Dance In The Duck - Right-click to exit");
        
        Shell_NotifyIconW(NIM_ADD, &nid);

        audioThread = std::thread(&KeyboardSoundApp::audioWorkerThread, this);
        return true;
    }

    void Stop() {
        running = false;
        if (audioThread.joinable()) {
            audioThread.join();
        }
        if (hookHandle) {
            UnhookWindowsHookEx(hookHandle);
            hookHandle = NULL;
        }
        Shell_NotifyIconW(NIM_DELETE, &nid);
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = NULL;
        }
        pressedKeys.clear();
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            hMutex = NULL;
        }
    }

    void onKeyPressed(DWORD vkCode) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        auto now = std::chrono::steady_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastSoundTime).count();
        if (elapsed < DEBOUNCE_MS) {
            return;
        }
        
        lastSoundTime = now;
        keyQueue.push(vkCode);
    }
    
    void onKeyReleased(DWORD vkCode) {
        std::lock_guard<std::mutex> lock(queueMutex);
        pressedKeys.erase(vkCode);
    }

    void Run() {
        MSG msg;
        while (running && GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

KeyboardSoundApp* KeyboardSoundApp::g_pApp = nullptr;

int MainApp() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    
    std::wstring soundPath = exeDir + L"\\sound\\audio.wav";
    
    KeyboardSoundApp app(soundPath);
    
    if (!app.Start()) {
        return 1;
    }
    
    app.Run();
    
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return MainApp();
}
