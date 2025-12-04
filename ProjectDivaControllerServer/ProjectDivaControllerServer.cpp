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
#include <boost/container/static_vector.hpp>
#define BOOST_NOWIDE_NO_LIB
#include <boost/nowide/utf/convert.hpp>
#include "HelperFunctionAndClass.h"
#pragma comment(lib, "ws2_32.lib")


constexpr unsigned short DISCOVERY_PORT = 39831;
constexpr unsigned short DAFULT_SERVICE_PORT = 3939;
std::atomic<unsigned short> g_service_port = DAFULT_SERVICE_PORT;
std::atomic<bool> g_running(true);

double g_slide_require_multiplier = 1.0;
#ifdef _DEBUG
bool g_output_received_message = true;
bool g_output_keyboard_operation = true;
#else
bool g_output_received_message = false;
bool g_output_keyboard_operation = false;
#endif // DEBUG
bool g_test_connection_stability = true;

std::array<BYTE, 8> vk_button{
    'U',
    'H',
    'J',
    'K',
    VK_UP,
    VK_LEFT,
    VK_DOWN,
    VK_RIGHT,
};
auto vk_stick = [vk_s = std::array<BYTE, 5>{'1', 'A', '\0', 'D', '3'}]
(int stick) mutable ->BYTE& {
    return vk_s.at(stick + 2);
    };

//負責解析輸入並模擬按下按鍵
class Controller {
    struct PointerInfo {
        int x_coordinate = 0;
        int next_x_coordinate = 0;
        int momentum = 0;
        int8_t pressingButton = 0;// 0保留給未按下 // 1/2/3/4 / 5/6/7/8
        int8_t pressingDirectionalButton = 0; //負代表向左，正代表向右  1是左邊的搖桿；2是右邊
        DWORD press_time = 0;
    };
    std::array<PointerInfo, 20> map_ID_cache;
    int later_up_count;
    int m_width;
    float m_xdpi;
    float m_sliding_demand_multiplier; // sqrt(Physical width)
    struct {
        std::array<bool, 8> buttons;
        std::array<DWORD, 4> button_up_time;//主要按鍵上次抬起的時間；0代表最後抬起的是次要按鍵
        std::array<int16_t, 2>sticks;
    }keybd_state{};
    std::chrono::nanoseconds last_update_time;
public:
    
    Controller(int width, int height, float xdpi, float ydpi) :
        m_width(width), m_xdpi(xdpi), map_ID_cache(), keybd_state(), later_up_count(),
        last_update_time(time_since_epoch()), 
        m_sliding_demand_multiplier(sqrt(width/ xdpi)){
    }
    Controller() :Controller(1, 1, 1, 1) {};
    //return true 代表在在一小段時間後必須呼叫FlushLaterUp()
    bool OnTouchAction(const char* event) {
        switch (event[0]) {
        case 'P': {
            // pong
            // format:
            // "PONG" time_since_epoch
            auto now = time_since_epoch();

            long long timept = atoll(event + 4);
            if (timept == 0) {
                printError("PING returned an incorrect PONG format.\n");
            }
            else {
                std::print("Round-Trip Time: {}ns\n", format_thousands_separator(now.count() - timept));
            }
            return 0;
        }
        case 'D': {
            // pointer down
            // format:
            // 'D' ID x y
            cheap_istrstream iss(event + 1);
            int pointer_ID = iss.getInt();
            int x = iss.getInt();
            int button_index = x * 4 / m_width;
            if (button_index >= 4 || button_index < 0) {
                throw std::runtime_error("Invalid touch point coordinates");
            }
            map_ID_cache.at(pointer_ID) = {};
            map_ID_cache[pointer_ID].x_coordinate = x;
            map_ID_cache[pointer_ID].next_x_coordinate = x;
            if (keybd_state.buttons[button_index] && keybd_state.buttons[button_index + 4]) {
                //兩個按鍵都已經按下去了，忽略這第三根手指
            }
            else {
                if (!keybd_state.buttons[button_index] && !keybd_state.buttons[button_index + 4]) {
                    //兩個按鍵都沒被按著
                    //優先選擇主要按鍵，除非上一個抬起來的是主要按鍵，且才剛被抬起來
                    if (keybd_state.button_up_time[button_index] != 0
                        && GetTickCount32() - keybd_state.button_up_time[button_index] < 100) {
                        button_index += 4;
                    }
                }
                else {
                    //只有一個按鍵按著；選另一個
                    if (keybd_state.buttons[button_index]) {
                        button_index += 4;
                    }
                }
            }
            if (!keybd_state.buttons[button_index]) {
                //按下按鍵
                keybd_state.buttons[button_index] = true;
                map_ID_cache[pointer_ID].pressingButton = button_index + 1;
                map_ID_cache[pointer_ID].press_time = GetTickCount32();
                SendKeybdInput(vk_button[button_index]);
            }
            break;
        }
        case 'U': {
            // pointer up
            // format:
            // 'U' ID
            FlushMoveAction();
            cheap_istrstream iss(event + 1);
            int pointer_ID = iss.getInt();
            return ActionPointerUp(pointer_ID);
        }
        case 'C': {
            cleanup_keybd_state();
            //以紅字輸出
            SetConsoleColor(12);
            std::print("[GameController] ACTION_CANCEL\n");
            SetConsoleColor();
            MessageBeep(MB_ICONERROR);
            break;
        }
        case 'M': {
            // move
            // format:
            // 'M' ID x1 y1 ID x2 y2 ID x3 y3 ...
            cheap_istrstream iss(event + 2);
            do {
                int ID = iss.getInt();
                int x = iss.getInt();
                int y = iss.getInt();
                map_ID_cache.at(ID).next_x_coordinate = x;
            } while (iss.data() && *iss.data()==' ');
            break;
        }
        default:
            return 0;
        }
        return 0;
    }
    void FlushLaterUp() {
        SetConsoleColor(FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        for (;later_up_count > 0; --later_up_count) {
            map_ID_cache[map_ID_cache.size() - later_up_count].press_time = 0;
            ActionPointerUp(map_ID_cache.size() - later_up_count);
        }
        SetConsoleColor();
    }
    void FlushMoveAction() {
        SetConsoleColor(FOREGROUND_GREEN);
        
        const auto nexttp = time_since_epoch();
        const auto duration = nexttp - last_update_time;
        last_update_time = nexttp;
        struct ID_displacement {
            int ID;
            int displacement;
        };
        boost::container::static_vector<ID_displacement, 20> candidate;
        for (int i = 0; i < map_ID_cache.size(); ++i) {
            if (map_ID_cache[i].next_x_coordinate - map_ID_cache[i].x_coordinate!=0){
                candidate.push_back({ i,map_ID_cache[i].next_x_coordinate - map_ID_cache[i].x_coordinate });
            }
        }
        if (g_output_received_message) {
            for (auto i : candidate) {
                std::print("candidates:[ID:{} displacement:{}]", i.ID, i.displacement);
            }
            std::print("\n");
        }
        const int min_displacement_require = MinDisplacementRequire((std::min)(duration, std::chrono::nanoseconds(33'554'432)));
        const int max_momentum = MinDisplacementRequire(std::chrono::nanoseconds(16'777'216));
        const int reduce_displacement = MinDisplacementRequire(duration) / 8;
        //結算先前動量
        {
            //增加動量
            int max_momentum2 = max_momentum + reduce_displacement;
            for (auto i : candidate) {
                map_ID_cache[i.ID].momentum += i.displacement;
                if (abs(map_ID_cache[i.ID].momentum) > max_momentum2) {
                    map_ID_cache[i.ID].momentum =
                        (map_ID_cache[i.ID].momentum > 0) ?
                        max_momentum2 : -max_momentum2;
                }
            }

            //減少動量並抬起按鍵
            for (auto& i : map_ID_cache) {
                if (i.momentum > reduce_displacement) {
                    i.momentum -= reduce_displacement;
                }
                else if (i.momentum < -reduce_displacement) {
                    i.momentum += reduce_displacement;
                }
                else {
                    i.momentum = 0;
                }
                if (i.pressingDirectionalButton) {
                    if (i.pressingDirectionalButton * i.momentum <= 0) {
                        //動量消耗殆盡或方向反了
                        //抬起滑鍵
                        keybd_state.sticks.at(abs(i.pressingDirectionalButton) - 1) = 0;
                        SendKeybdInput(vk_stick(i.pressingDirectionalButton), KEYEVENTF_KEYUP);
                        i.pressingDirectionalButton = 0;
                    }
                    else {
                        //把此手指從候選者中剃除
                        for (auto& cand : candidate) {
                            if (cand.ID == std::addressof(i) - map_ID_cache.data()) {
                                
                                cand.displacement = 0;
                            }
                        }
                    }
                }
            }
        }
        
        std::sort(candidate.begin(), candidate.end(), [](ID_displacement a, ID_displacement b) {
            return abs(a.displacement) > abs(b.displacement);
            });

        if (g_output_received_message && candidate.size() > 0) {
            std::print("min_displacement_require:{} maxdisplacement:{} {}\n\n"
                , min_displacement_require, candidate.front().displacement, candidate.front().ID);
        }
        int freestickcount = (keybd_state.sticks[0] == 0) + (keybd_state.sticks[1] == 0);
        if (freestickcount == 2
            && candidate.size() >= 2
            && abs(candidate[1].displacement) > min_displacement_require) {
            auto candidateL = candidate.front();
            auto candidateR = candidate[1];
            if (map_ID_cache[candidateL.ID].x_coordinate > map_ID_cache[candidateR.ID].x_coordinate) {
                std::swap(candidateL, candidateR);
            }
            map_ID_cache[candidateL.ID].pressingDirectionalButton =
                keybd_state.sticks[0] =
                candidateL.displacement > 0 ? 1 : -1;
            map_ID_cache[candidateL.ID].press_time = GetTickCount32();

            map_ID_cache[candidateR.ID].pressingDirectionalButton =
                keybd_state.sticks[1] =
                candidateR.displacement > 0 ? 2 : -2;
            map_ID_cache[candidateR.ID].press_time = GetTickCount32();
            //給予初始動量
            {
                map_ID_cache[candidateL.ID].momentum += candidateL.displacement;
                if (abs(map_ID_cache[candidateL.ID].momentum) > max_momentum) {
                    map_ID_cache[candidateL.ID].momentum =
                        (map_ID_cache[candidateL.ID].momentum > 0) ?
                        max_momentum : -max_momentum;
                }
                map_ID_cache[candidateR.ID].momentum += candidateR.displacement;
                if (abs(map_ID_cache[candidateR.ID].momentum) > max_momentum) {
                    map_ID_cache[candidateR.ID].momentum =
                        (map_ID_cache[candidateR.ID].momentum > 0) ?
                        max_momentum : -max_momentum;
                }
            }
            INPUT input[2]{};
            input[0].type = input[1].type = INPUT_KEYBOARD;
            input[0].ki.wVk = vk_stick(keybd_state.sticks[0]);
            input[1].ki.wVk = vk_stick(keybd_state.sticks[1]);
            SendInput(2, input, sizeof(INPUT));
            std::print("{} {}  [DOWN]\n",
                keybd_state.sticks[0] > 0 ? "->" : "<-",
                keybd_state.sticks[1] > 0 ? "->" : "<-");
        }
        else if (freestickcount >= 1
            && candidate.size() >= 1
            && abs(candidate[0].displacement) > min_displacement_require) {
            auto Candidate = candidate.front();
            int LR;
            if (freestickcount == 2) {
                LR = 1 + (map_ID_cache[Candidate.ID].x_coordinate > m_width / 2);
            }
            else {
                LR = keybd_state.sticks[0] == 0 ? 1 : 2;
            }
            map_ID_cache[Candidate.ID].pressingDirectionalButton =
                keybd_state.sticks[LR - 1] =
                Candidate.displacement > 0 ? LR : -LR;
            map_ID_cache[Candidate.ID].press_time = GetTickCount32();

            //給予初始動量
            map_ID_cache[Candidate.ID].momentum += Candidate.displacement;
            if (abs(map_ID_cache[Candidate.ID].momentum) > max_momentum) {
                map_ID_cache[Candidate.ID].momentum =
                    (map_ID_cache[Candidate.ID].momentum > 0) ?
                    max_momentum : -max_momentum;
            }
            SendKeybdInput(vk_stick(keybd_state.sticks[LR - 1]));
        }
        for (auto& i : map_ID_cache) {
            i.x_coordinate = i.next_x_coordinate;
        }
        SetConsoleColor();
    }
    ~Controller() {
        cleanup_keybd_state();
    }
private:
    int MinDisplacementRequire(std::chrono::nanoseconds duration) const noexcept {
        return static_cast<int>(m_xdpi* duration.count()* m_sliding_demand_multiplier*g_slide_require_multiplier / 268'435'456);// 在10英吋的平板上，約12in/s
    }
    bool ActionPointerUp(int pointer_ID) {
        if (map_ID_cache.at(pointer_ID).pressingButton || map_ID_cache[pointer_ID].pressingDirectionalButton) {
            if (20 > GetTickCount32() - map_ID_cache[pointer_ID].press_time) {
                //This pointer才剛按下某個按鍵，為了確保此按鍵能被遊戲偵測到，稍微延遲1幀的時間再抬起
                ++later_up_count;
                map_ID_cache[map_ID_cache.size() - later_up_count] = map_ID_cache[pointer_ID];
                map_ID_cache[pointer_ID] = {};
                return true;
            }
            if (map_ID_cache[pointer_ID].pressingButton) {
                int button_index = map_ID_cache[pointer_ID].pressingButton - 1;
                keybd_state.buttons.at(button_index) = false;
                SendKeybdInput(vk_button[button_index], KEYEVENTF_KEYUP);
                if (button_index < 4) {
                    keybd_state.button_up_time[button_index] = GetTickCount32();
                }
                else {
                    keybd_state.button_up_time[button_index - 4] = 0;
                }
            }
            if (map_ID_cache[pointer_ID].pressingDirectionalButton) {
                keybd_state.sticks.at(abs(map_ID_cache[pointer_ID].pressingDirectionalButton) - 1) = 0;
                SendKeybdInput(vk_stick(map_ID_cache[pointer_ID].pressingDirectionalButton), KEYEVENTF_KEYUP);
            }
        }
        map_ID_cache[pointer_ID] = {};
        return false;
    }
    void SendKeybdInput(BYTE vk_code,DWORD Flags=NULL) {
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
    void cleanup_keybd_state() {
        map_ID_cache = {};
        for (int i = 0; i < keybd_state.buttons.size(); ++i) {
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
        std::string resp = "Miku here: " + std::to_string(g_service_port);
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

        static constexpr LPCWSTR tester_UI_title = L"Connection stability test finished";
        try {
            // receive loop
            const int BUF_SZ = 2048;
            std::vector<char> buffer(BUF_SZ);
            Controller controller;
            NetStabilityMeter connection_tester;
            
            auto sendPing = [client]() {
                std::string msg = std::format("PING {}\n", time_since_epoch().count());
                std::print("<-{}", msg);
                if (SOCKET_ERROR == send(client, msg.c_str(), msg.length(), NULL)) {
                    printWSAError("send(PING)");
                }};
            sendPing();
            auto sendTestRequest = [client]() {
                std::print("<-Test\n");
                if (SOCKET_ERROR == send(client, "Test\n", 5, NULL)) {
                    printWSAError("send(Test)");
                }};
            if (g_test_connection_stability) {
                sendTestRequest();
            }
            bool flash_laterUp = true;
            for (bool idle = true; g_running.load();) {
                fd_set rfds2;
                FD_ZERO(&rfds2);
                FD_SET(client, &rfds2);

                timeval tv2 = flash_laterUp ? timeval{ 0, 14'000 } : timeval{ 5,0 };
                if (0 < select(0, &rfds2, nullptr, nullptr, &tv2)) {
                    idle = false;
                    flash_laterUp = false;
                    int bytes = recv(client, buffer.data(), BUF_SZ - 2, 0);

                    if (bytes > 0) {
                        controller.FlushLaterUp();
                        flash_laterUp = false;
                        buffer[bytes] = buffer[bytes + 1] = '\0';
                        if (g_output_received_message) {
                            std::print("->{}\n", buffer.data());
                        }

                        for (const char* p = buffer.data(), *pend = p + bytes;
                            *p != '\0';
                            p = std::find(p, pend, '\n') + 1) {
                            switch (p[0]) {
                            case 'D':
                            case 'U':
                            case 'C':
                            case 'M':
                            case 'P':
                                flash_laterUp |= controller.OnTouchAction(p);
                                break;
                            case 'S': {
                                //format:"SCREENSIZE:" width height xdpi ydpi devicename
                                cheap_istrstream iss(p);
                                iss.skip();
                                int width = iss.getInt();
                                int height = iss.getInt();
                                double xdpi = iss.getDouble();
                                double ydpi = iss.getDouble();
                                controller = Controller(width, height, xdpi, ydpi);
                                break;
                            }
                            case 'T': {
                                //format:"Test" time_point
                                cheap_istrstream iss(p);
                                iss.skip();
                                long long time_point= iss.getLLInt();
                                if (time_point == 0) {
                                    printError("Invalid Test message\n");
                                    break;
                                }
                                if (connection_tester.AddSamples(time_point - time_since_epoch().count())) {
                                    std::thread(
                                        [sendTestRequest]() {
                                            if (IDRETRY == MessageBoxW(NULL,
                                                L"You can press the \"Retry\" button to test again, or close this window.",
                                                tester_UI_title,
                                                MB_RETRYCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON2))
                                            {
                                                sendTestRequest();
                                            }
                                            return;
                                        }
                                    ).detach();
                                }
                            }
                            }
                        }
                        controller.FlushMoveAction();
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
                    controller.FlushLaterUp();
                    flash_laterUp = false;
                }
                else {
                    if (idle) {
                        std::print("[TCP] Connection timed out.\n");
                        break;
                    }
                    idle = true;
                    sendPing();
                }
            }
        }
        catch(std::exception& e){
            printError("[TCP] An exception was thrown: {}\n"
                "[TCP] Terminate connection...\n", e.what());
            MessageBeep(MB_ICONERROR);
        }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        PostMessageW(FindWindowW(NULL, tester_UI_title), WM_CLOSE, NULL, NULL);
        closesocket(client);
    }

    closesocket(listenSock);
    std::print("[TCP] Service stopped.\n");
}

BOOL WINAPI consoleHandler(DWORD signal) {
    g_running.store(false);
    Sleep(4500);
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
        if (std::getline(file, str) && str.size() >= 1) {
            double temp = atof(str.c_str());
            if (temp) {
                g_slide_require_multiplier = temp;
                std::getline(file, str)
                    && ((g_output_received_message = atoi(str.c_str())), std::getline(file, str))
                    && ((g_output_keyboard_operation = atoi(str.c_str())), std::getline(file, str))
                    && (g_test_connection_stability = atoi(str.c_str()));
            }
            else {
                error = true;
                goto err;
            }
        }
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
        "slide_require_multiplier : {:.2f}\n"
        "output_received_message : {}\n"
        "output_keyboard_operation : {}\n"
        "test_connection_stability : {}\n"
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
        , g_slide_require_multiplier
        , g_output_received_message
        , g_output_keyboard_operation
        , g_test_connection_stability
    );
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleCtrlHandler(nullptr, TRUE);//ignore CTRL+C
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
    std::print("\nShutting down...\n");
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
