// ProjectDivaControllerServer.cpp : 此檔案包含 'main' 函式。程式會於該處開始執行及結束執行。
//
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma execution_character_set("utf-8")
// Windows (Winsock) UDP discovery + TCP service
//  - UDP discovery port: 39831
//  - TCP service port:   3939
//
// Notes:
//  - Allow program through Windows Firewall when prompted.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <format>
#include <print>
#include <fstream>
#include <array>
#include <chrono>
#include <cassert>
#include <algorithm>
#define BOOST_NOWIDE_NO_LIB
#include <boost/nowide/utf/convert.hpp>
#include "HelperFunctionAndClass.h"
#pragma comment(lib, "ws2_32.lib")


constexpr unsigned short DISCOVERY_PORT = 39831;
constexpr unsigned short DAFULT_SERVICE_PORT = 3939;
// I had originally planned to use another port if port 3939 was occupied, but I never encountered this situation.
std::atomic<unsigned short> g_service_port = DAFULT_SERVICE_PORT;
std::atomic<bool> g_running(true);

#ifdef _DEBUG
bool g_output_received_message = true;
bool g_output_keyboard_operation = true;
#else
bool g_output_received_message = false;
bool g_output_keyboard_operation = false;
#endif // DEBUG


std::array<BYTE, 8> vk_button{
    'I',
    'J',
    'K',
    'L',
    'W',
    'A',
    'S',
    'D',
};
auto vk_stick = [vk_s = std::array<BYTE, 5>{'Q', 'U', '\0', 'E', 'O'}]
(int stick) mutable ->BYTE& {
    return vk_s.at(stick + 2);
    };

//負責模擬按下按鍵並管理按鍵狀態
class Controller {
    struct {
        std::array<bool, 8> buttons{};
        std::array<int16_t, 2> sticks{};
        std::array<bool, 8> pendingLaterUp{};
        std::array<bool, 2> pendingStickLaterUp{};
        std::array<std::chrono::nanoseconds, 8> button_downTime{};
        std::array<std::chrono::nanoseconds, 2> stick_downTime{};
    } keybd_state{};

    // MM+ polls the keyboard state once per frame, instead of through keyboard messages.
    // To prevent input from being lost, ensure that each keystroke is maintained for at least one frame.
    static constexpr std::chrono::nanoseconds min_keepdown_time = std::chrono::nanoseconds(16'600'000);
public:
    void ButtonDown(BYTE index) {
        if (keybd_state.pendingLaterUp.at(index)) {
            // A release was deferred; honor it now before re-pressing, so the game
            // still sees a full up/down transition instead of one continuous hold.
            keybd_state.buttons.at(index) = false;
            SetConsoleColor(FOREGROUND_BLUE | FOREGROUND_GREEN);
            SendKeybdInput(vk_button.at(index), KEYEVENTF_KEYUP);
            SetConsoleColor();
            keybd_state.pendingLaterUp[index] = false;
        }
        keybd_state.buttons.at(index) = true;
        SendKeybdInput(vk_button.at(index));
        keybd_state.button_downTime.at(index) = time_since_epoch();
    }
    void ButtonUp(BYTE index) {
        std::chrono::nanoseconds now = time_since_epoch();
        if ((now - keybd_state.button_downTime.at(index)) < min_keepdown_time) {
            keybd_state.pendingLaterUp.at(index) = true;
            return;
        }
        keybd_state.buttons.at(index) = false;
        SendKeybdInput(vk_button.at(index), KEYEVENTF_KEYUP);
    }
    void StickDown(char index) {
        size_t i = std::abs(index) - 1;
        if (keybd_state.pendingStickLaterUp.at(i)) {
            SetConsoleColor(FOREGROUND_BLUE | FOREGROUND_GREEN);
            SendKeybdInput(vk_stick(keybd_state.sticks.at(i)), KEYEVENTF_KEYUP);
            SetConsoleColor();
            keybd_state.pendingStickLaterUp[i] = false;
        }
        keybd_state.sticks.at(i) = index;
        SendKeybdInput(vk_stick(index));
        keybd_state.stick_downTime.at(i) = time_since_epoch();
    }
    void StickUp(char index) {
        size_t i = std::abs(index) - 1;
        std::chrono::nanoseconds now = time_since_epoch();
        if ((now - keybd_state.stick_downTime.at(i)) < min_keepdown_time) {
            keybd_state.pendingStickLaterUp.at(i) = true;
            return;
        }
        keybd_state.sticks.at(i) = 0;
        SendKeybdInput(vk_stick(index), KEYEVENTF_KEYUP);
    }
    void SendKeybdInput(BYTE vk_code, DWORD Flags = NULL) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk_code;
        input.ki.dwFlags = Flags;
        SendInput(1, &input, sizeof(INPUT));
        if (g_output_keyboard_operation) {
            std::print("{} [{}]\n"
                "keybd_state:\n"
                "{:d} {:d} {:d} {:d}\n"
                "{:d} {:d} {:d} {:d}\n"
                "[{}{}]\n", vkToString(vk_code), Flags ? "UP" : "DOWN",
                keybd_state.buttons[0],
                keybd_state.buttons[1],
                keybd_state.buttons[2],
                keybd_state.buttons[3],
                keybd_state.buttons[4],
                keybd_state.buttons[5],
                keybd_state.buttons[6],
                keybd_state.buttons[7],
                keybd_state.sticks[0] == 0 ? " • " : keybd_state.sticks[0] > 0 ? " •>" : "<• ",
                keybd_state.sticks[1] == 0 ? " • " : keybd_state.sticks[1] > 0 ? " •>" : "<• "
            );
        }
    }

    int FlushLaterUp() {
        std::chrono::nanoseconds now = time_since_epoch();
        int stillPending = 0;
        for (size_t idx = 0; idx < keybd_state.pendingLaterUp.size(); ++idx) {
            if (!keybd_state.pendingLaterUp[idx]) continue;
            if (now - keybd_state.button_downTime[idx] > min_keepdown_time) {
                keybd_state.buttons[idx] = false;
                SetConsoleColor(FOREGROUND_BLUE | FOREGROUND_GREEN);
                SendKeybdInput(vk_button[idx], KEYEVENTF_KEYUP);
                SetConsoleColor();
                keybd_state.pendingLaterUp[idx] = false;
            }
            else {
                ++stillPending;
            }
        }
        for (size_t idx = 0; idx < keybd_state.pendingStickLaterUp.size(); ++idx) {
            if (!keybd_state.pendingStickLaterUp[idx]) continue;
            if (now - keybd_state.stick_downTime[idx] > min_keepdown_time) {
                BYTE vk = vk_stick(keybd_state.sticks[idx]);
                keybd_state.sticks[idx] = 0;
                SetConsoleColor(FOREGROUND_BLUE | FOREGROUND_GREEN);
                SendKeybdInput(vk, KEYEVENTF_KEYUP);
                SetConsoleColor();
                keybd_state.pendingStickLaterUp[idx] = false;
            }
            else {
                ++stillPending;
            }
        }
        return stillPending;
    }
    void cleanup_keybd_state() {
        for (size_t i = 0; i < keybd_state.buttons.size(); ++i) {
            if (keybd_state.buttons[i]) {
                SendKeybdInput(vk_button[i], KEYEVENTF_KEYUP);
            }
        }
        if (keybd_state.sticks[0]) {
            SendKeybdInput(vk_stick(keybd_state.sticks[0]), KEYEVENTF_KEYUP);
        }
        if (keybd_state.sticks[1]) {
            SendKeybdInput(vk_stick(keybd_state.sticks[1]), KEYEVENTF_KEYUP);
        }
        keybd_state = {};
    }
    ~Controller() {
        cleanup_keybd_state();
    }
};

// UDP discovery server: listen for discovery packets and reply "Miku here:3939"
static void udpDiscoveryServer() {
    SOCKET sock = INVALID_SOCKET;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printWSAError("[UDP] socket()");
        return;
    }

    // bind to any address on DISCOVERY_PORT
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(DISCOVERY_PORT);

    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        printWSAError("[UDP] bind()");
        closesocket(sock);
        return;
    }
    
    std::print("\n[UDP] Discovery server listening on port {}\n\n", DISCOVERY_PORT);

    // set a recv timeout so thread can check g_running periodically
    DWORD timeout = 3939; // ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    char buf[512];
    while (g_running.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, buf, (int)sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                // timeout - check running flag
                continue;
            }
            else {
                printWSAError("[UDP] recvfrom()");
                break;
            }
        }
        buf[n] = '\0';
        char fromIp[INET_ADDRSTRLEN] = { 0 };
        inet_ntop(AF_INET, &from.sin_addr, fromIp, sizeof(fromIp));
        std::print("[UDP] Received from {}:{} -> {}\n", fromIp, ntohs(from.sin_port), buf);

        // Simple protocol: respond with HERE:PORT
        std::string resp = std::format("Miku here: {}", g_service_port.load());
        int sent = sendto(sock, resp.c_str(), (int)resp.size(), 0, reinterpret_cast<sockaddr*>(&from), fromLen);
        if (sent == SOCKET_ERROR) {
            printWSAError("[UDP] sendto()");
            // continue - discovery best-effort
        }
        else {
            std::print("[UDP] Replied to {} -> {}\n", fromIp, resp);
        }
    }

    closesocket(sock);
    std::print("[UDP] Discovery server stopped.\n");
}

// TCP service: accept a single client, print incoming data. If client disconnects, can accept again.
static void tcpService() {

    SOCKET listenSock = INVALID_SOCKET;
    listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        printWSAError("TCP socket()");
        return;
    }

    // allow immediate reuse of address
    {
        int opt = 1;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    }
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY; // listen on all interfaces
    srv.sin_port = htons(g_service_port);

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) == SOCKET_ERROR) {
        printWSAError("[TCP] bind()");
        closesocket(listenSock);
        return;
    }
    if (listen(listenSock, 1) == SOCKET_ERROR) {
        printWSAError("[TCP] listen()");
        closesocket(listenSock);
        return;
    }
    listLocalIPsAndAdapters();
    std::print("[TCP] Service listening on port {}\n", g_service_port.load());

    for (int count = 0; g_running.load(); ++count) {
        std::print("[TCP] Waiting for client...\n");
        // accept will block; use select with timeout to allow checking g_running occasionally
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listenSock, &rfds);
        timeval tv{3,9};// 3.000009 seconds
        int sel = select(0, &rfds, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR) {
            printWSAError("select()");
            break;
        }
        else if (sel == 0) {
            // timeout
            continue;
        }

        SOCKET client = accept(listenSock, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            printWSAError("accept()");
            continue;
        }

        // 關閉 Nagle 演算法
        {
            BOOL flag = TRUE;
            if (setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag))) {
                printWSAError("setsockopt(TCP_NODELAY)");
            }
        }
        sockaddr_in peer{};
        int peerLen = sizeof(peer);
        getpeername(client, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        char peerIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, peerIp, sizeof(peerIp));
        
        std::print("[TCP] Client connected from {}:{}\n", peerIp, ntohs(peer.sin_port));
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        try {
            // receive loop
            const int BUF_SZ = 1016;
            std::vector<char> buffer(BUF_SZ);
            Controller controller;
            NetStabilityMeter2 connection_tester;
            //除了保持連線外，也順便做連線延遲測試
            auto sendPing = [client]() {
                long long time = time_since_epoch().count();
                static_assert(sizeof(time) == 8);
                if (SOCKET_ERROR == send(client, reinterpret_cast<const char*>(&time), 8, NULL)) {
                    printWSAError("send(PING)");
                }};
            sendPing();
            bool flash_laterUp = false;
            for (bool idle = true; g_running.load();) {
                fd_set rfds2;
                FD_ZERO(&rfds2);
                FD_SET(client, &rfds2);

                // 為了避免喚醒延遲，當需要flash_laterUp時將採用輪詢的方式
                // 這種情況應該不常發生，因此浪費些CPU時間應該沒關係
                // 不過，在透過無線網路連接時，發生的機率會較高
                timeval tv2 = flash_laterUp ? timeval{ 0, 0 } : timeval{ 4, 500000 };
                if (0 < select(0, &rfds2, nullptr, nullptr, &tv2)) {
                    if (idle) {
                        idle = false;
                        std::println("[TCP] ->PONG");
                    }
                    // 保留足夠空間以預防無效輸入導致讀取超出邊界
                    int bytes = recv(client, buffer.data(), BUF_SZ - 16, 0);
                    if (bytes > 0) {
                        if (g_output_received_message) {
                            std::string s;
                            s.reserve(bytes * 2);
                            for (int i = 0; i < bytes; ++i) {
                                std::format_to(std::back_inserter(s), "{:02X}", static_cast<BYTE>(buffer[i]));
                            }
                            std::println("->{}", s);
                        }
                        for (const char* p = buffer.data(), *pend = p + bytes;p<pend;) {
                            switch (p[0]) {
                            case 'D': {
                                controller.ButtonDown(p[1]);
                                p += 2;
                                break;
                            }
                            case 'U': {
                                controller.ButtonUp(p[1]);
                                p += 2;
                                break;
                            }
                            case 'd': {
                                controller.StickDown(p[1]);
                                p += 2;
                                break;
                            }
                            case 'u': {
                                controller.StickUp(p[1]);
                                p += 2;
                                break;
                            }
                            case 'C': {
                                controller.cleanup_keybd_state();
                                p += 1;
                                break;
                            }
                            case 'R': {
                                INT64 serverSendTime = *(reinterpret_cast<const UNALIGNED INT64*>(p + 1));
                                INT64 clientRevcTime = *(reinterpret_cast<const UNALIGNED INT64*>(p + 9));
                                INT64 serverRevcTime = time_since_epoch().count();
                                int res = connection_tester.AddSample(serverSendTime, clientRevcTime, serverRevcTime);
                                if (res) sendPing();
                                p += 17;
                                break;
                            }
                            default:
                                p = pend;
                                printError("Unknown message");
                                MessageBeep(MB_ICONERROR);
                                break;
                            }
                        }
                        flash_laterUp = controller.FlushLaterUp();
                    }
                    else if (bytes == 0) {
                        std::print("[TCP] Client disconnected.\n");
                        break;
                    }
                    else {
                        int err = WSAGetLastError();
                        if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
                            continue;
                        }
                        else {
                            printWSAError("recv()");
                            break;
                        }
                    }
                }
                else if (flash_laterUp) {
                    //我們正瘋狂自旋輪詢中，可能持續數毫秒之久。姑且降低些CPU功耗吧
                    //這能顯著降低輪詢的次數(從數萬次降至數百次)，但是不是真能降低功耗我也不知道
                    for (int i = 0; i < 8192; ++i) {
                        _mm_pause();
                    }
                    flash_laterUp = controller.FlushLaterUp();
                }
                else {
                    if (idle) {
                        std::print("[TCP] Connection timed out.\n");
                        break;
                    }
                    else {
                        idle = true;
                        std::println("[TCP] <-PING");
                        sendPing();
                    }
                }
            }
        }
        catch(std::exception& e){
            printError("[TCP] An exception was thrown: {}\n"
                "[TCP] Terminate connection...\n", e.what());
            MessageBeep(MB_ICONERROR);
        }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        closesocket(client);
    }

    closesocket(listenSock);
    std::print("[TCP] Service stopped.\n");
}

BOOL WINAPI consoleHandler(DWORD signal) {
    g_running.store(false);
    Sleep(4800);
    return FALSE;
}

static void ReadAndPrintSettings() {
    std::ifstream file("ProjectDivaControllerSettings.txt");

    if (file.is_open()) {

        std::string str;
        auto SetVk = [&str](BYTE& value) ->bool{
            if (str.size() >= 1 && (str[1] == '\0' || isspace(str[1]))) {
                if (isupper(str[0])) {
                    value = str[0];
                    return 0;
                }
                else {
                    return -1;
                }
            }
            else {
                int vk = atoi(str.c_str());
                if (vk <= 0 || vk > 255) {
                    return -1;
                }
                else {
                    value = vk;
                    return 0;
                }
            }
            };
        bool error = false;
        for (int i = 0; i < 8 && std::getline(file, str); ++i)
        {
            if (SetVk(vk_button[i])) {
                error = true;
                goto err;
            }
        }
        //這裡使用了or的短路求值
        error = !std::getline(file, str) || SetVk(vk_stick(-1))
            || !std::getline(file, str) || SetVk(vk_stick(1))
            || !std::getline(file, str) || SetVk(vk_stick(-2))
            || !std::getline(file, str) || SetVk(vk_stick(2));
        //以及comma , operator
        std::getline(file, str)
            && ((g_output_received_message = atoi(str.c_str())), std::getline(file, str))
            && ((g_output_keyboard_operation = atoi(str.c_str())), std::getline(file, str));
        if (error) {
            err:
            printError("The \"ProjectDivaControllerSettings.txt\" file does not contain enough settings or format incorrect; the rest will use default values.");
        }
    }
    else {
        printError("Can't open \"ProjectDivaControllerSettings.txt\"\n"
            "using default settings\n");
    }
    std::print("Settings:\n"
        "{} : {}\n"
        "{} : {}\n"
        "{} : {}\n"
        "{} : {}\n"
        "{} : {}\n"
        "{} : {}\n"
        "{} : {}\n"
        "{} : {}\n"
        "{}\n{} {}\t{} {}\n"
        "output_received_message : {}\n"
        "output_keyboard_operation : {}\n"
        , "△", vkToString(vk_button[0])
        , "□", vkToString(vk_button[1])
        , "×", vkToString(vk_button[2])
        , "◯", vkToString(vk_button[3])
        , "🡅", vkToString(vk_button[4])
        , "🡄", vkToString(vk_button[5])
        , "🡇", vkToString(vk_button[6])
        , "🡆", vkToString(vk_button[7])
        , "↼ ⇀\t↼ ⇀"
        , vkToString(vk_stick(-1)), vkToString(vk_stick(1)), vkToString(vk_stick(-2)), vkToString(vk_stick(2))
        , g_output_received_message
        , g_output_keyboard_operation
    );
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    //UTF8萬歲! 亂碼再見!
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    ReadAndPrintSettings();

    WSADATA wsaData;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc != 0) {
        printError("WSAStartup failed: {}\n", rc);
        return 1;
    }

    std::print("Touch server starting.\n");

    std::thread udpThread(udpDiscoveryServer);
    std::thread tcpThread(tcpService);

    // wait for threads to finish
    tcpThread.join();
    g_running.store(false);
    udpThread.join();

    WSACleanup();
    return 0;
}

// 執行程式: Ctrl + F5 或 [偵錯] > [啟動但不偵錯] 功能表
// 偵錯程式: F5 或 [偵錯] > [啟動偵錯] 功能表

// 開始使用的提示: 
//   1. 使用 [方案總管] 視窗，新增/管理檔案
//   2. 使用 [Team Explorer] 視窗，連線到原始檔控制
//   3. 使用 [輸出] 視窗，參閱組建輸出與其他訊息
//   4. 使用 [錯誤清單] 視窗，檢視錯誤
//   5. 前往 [專案] > [新增項目]，建立新的程式碼檔案，或是前往 [專案] > [新增現有項目]，將現有程式碼檔案新增至專案
//   6. 之後要再次開啟此專案時，請前往 [檔案] > [開啟] > [專案]，然後選取 .sln 檔案
