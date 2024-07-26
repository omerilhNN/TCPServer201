#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")
#define IP "192.168.254.16"

using namespace std;

queue<HANDLE> eventQueue;
vector<SOCKET> clients;
mutex queueMutex, vectorMutex;
vector<thread> socketThreads;
const int PORT = 36;
bool managerActive = true;

DWORD WINAPI SocketHandler(LPVOID lpParam) {
    SOCKET serverSocket = *reinterpret_cast<SOCKET*>(lpParam);
    WSAEVENT wsaEvent = WSACreateEvent();
    if (WSAEventSelect(serverSocket, wsaEvent, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR) {
        cerr << "WSAEventSelect failed with error: " << WSAGetLastError() << endl;
        closesocket(serverSocket);
        return 1;
    }

    cout << "Waiting for connection" << endl;
    HANDLE events[1] = { wsaEvent };

    while (true) {
        // Wait for event to occur.
        DWORD waitResult = WSAWaitForMultipleEvents(1, events, FALSE, INFINITE, TRUE);
        if (waitResult == WSA_WAIT_FAILED) {
            cerr << "WSAWaitForMultipleEvents Failed: " << WSAGetLastError() << endl;
            break;
        }
        if (waitResult == WAIT_OBJECT_0) {
            queueMutex.lock();
            eventQueue.push(wsaEvent);
            SetEvent(wsaEvent);
            queueMutex.unlock();
        }
    }

    closesocket(serverSocket);
    WSACloseEvent(wsaEvent);
    return 0;
}

DWORD WINAPI Manager(LPVOID lpParam) {
    SOCKET serverSocket = *reinterpret_cast<SOCKET*>(lpParam);

    while (managerActive) {
        queueMutex.lock();
        if (!eventQueue.empty()) {
            HANDLE sockEvent = eventQueue.front();
            eventQueue.pop();
            queueMutex.unlock();

            WSANETWORKEVENTS netEvents;
            if (WSAEnumNetworkEvents(serverSocket, sockEvent, &netEvents) == SOCKET_ERROR) {
                cerr << "WSAEnumNetworkEvents failed with error: " << WSAGetLastError() << endl;
                closesocket(serverSocket);
                break;
            }

            if (netEvents.lNetworkEvents & FD_ACCEPT) {
                SOCKET client = accept(serverSocket, nullptr, nullptr);
                if (client == INVALID_SOCKET) {
                    cerr << "Accept Failed: " << WSAGetLastError() << endl;
                    continue;
                }
                else {
                    vectorMutex.lock();
                    clients.push_back(client);
                    vectorMutex.unlock();
                    cout << "Client " << clients.size() - 1 << " accepted" << endl;
                }
            }

            if (netEvents.lNetworkEvents & FD_CLOSE) {
                cout << "Client disconnected" << endl;
                closesocket(serverSocket);
            }
        }
        else {
            queueMutex.unlock();
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
    return 0;
}

int main() {
    WSADATA wsaData;
    SOCKET serverSocket;
    sockaddr_in serv_addr;

    int wsa = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa != 0) {
        cerr << "WSAStartup failed: " << wsa << endl;
        exit(1);
    }
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        cerr << "Can't create a socket! Quitting" << endl;
        WSACleanup();
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, IP, &serv_addr.sin_addr) <= 0) {
        cout << "Invalid IP" << endl;
        return -1;
    }

    if (bind(serverSocket, (sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        cout << "Bind Failed" << endl;
        closesocket(serverSocket);
        return -1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        cout << "Listen failed" << endl;
        closesocket(serverSocket);
        return -1;
    }

    HANDLE hSocketHandlerThread = CreateThread(NULL, 0, SocketHandler, reinterpret_cast<LPVOID>(&serverSocket), 0, NULL);
    HANDLE hManagerThread = CreateThread(NULL, 0, Manager, reinterpret_cast<LPVOID>(&serverSocket), 0, NULL);

    WaitForSingleObject(hSocketHandlerThread, INFINITE);
    WaitForSingleObject(hManagerThread, INFINITE);

    CloseHandle(hSocketHandlerThread);
    CloseHandle(hManagerThread);

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
