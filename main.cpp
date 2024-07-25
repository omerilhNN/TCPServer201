#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

//struct SocketEvent {
//    SOCKET socket;
//    HANDLE event;
//};

queue<HANDLE> eventQueue;
mutex queueMutex;
vector<thread> socketThreads;
const int PORT = 36;
bool managerActive = true;

DWORD WINAPI SocketHandler(LPVOID lpParam) {
    SOCKET clientSocket = reinterpret_cast<SOCKET>(lpParam);
    HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (hEvent == NULL) {
        cerr << "CreateEvent failed with error: " << GetLastError() << endl;
        closesocket(clientSocket);
        return 1;
    }

    WSAEVENT wsaEvent = WSACreateEvent();
    if (WSAEventSelect(clientSocket, wsaEvent, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
        cerr << "WSAEventSelect failed with error: " << WSAGetLastError() << endl;
        closesocket(clientSocket);
        CloseHandle(hEvent);
        return 1;
    }

    HANDLE events[2] = { hEvent, wsaEvent };

    while (true) {
        DWORD waitResult = WaitForMultipleObjectsEx(2, events, FALSE, INFINITE,TRUE);

        if (waitResult == WAIT_OBJECT_0) {
            ResetEvent(hEvent);
        }
        else if (waitResult == WAIT_OBJECT_0 + 1) {
            // WSAEvent triggered, push to queue
            queueMutex.lock();
            eventQueue.push( wsaEvent);
            queueMutex.unlock();
            SetEvent(hEvent); // Notify manager
        }
    }

    closesocket(clientSocket);
    CloseHandle(hEvent);
    WSACloseEvent(wsaEvent);
    return 0;
}

// Manager thread function
DWORD WINAPI Manager(LPVOID lpParam) {
    SOCKET serverSocket = reinterpret_cast<SOCKET>(lpParam);

    while (managerActive) {
        queueMutex.lock();
        if (!eventQueue.empty()) {
            HANDLE sockEvent = eventQueue.front();
            eventQueue.pop();
            queueMutex.unlock();

            // Process the event based on the socket event
            WSANETWORKEVENTS netEvents;
            if (WSAEnumNetworkEvents(serverSocket, sockEvent, &netEvents) == SOCKET_ERROR) {
                cerr << "WSAEnumNetworkEvents failed with error: " << WSAGetLastError() << endl;
                continue;
            }

            if (netEvents.lNetworkEvents & FD_READ) {
                char buf[4096];
                int bytesReceived = recv(serverSocket, buf, 4096, 0);
                if (bytesReceived > 0) {
                    string receivedData(buf, 0, bytesReceived);
                    cout << "Received: " << receivedData << endl;
                }
                else {
                    cerr << "recv failed with error: " << WSAGetLastError() << endl;
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
    SOCKET listening;
    sockaddr_in hint;

    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        cerr << "WSAStartup failed: " << iResult << endl;
        exit(1);
    }
    listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listening == INVALID_SOCKET) {
        cerr << "Can't create a socket! Quitting" << endl;
        WSACleanup();
        exit(1);
    }

    hint.sin_family = AF_INET;
    hint.sin_port = htons(PORT);
    hint.sin_addr.S_un.S_addr = INADDR_ANY;

    if (bind(listening, (sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR) {
        cout << "Bind Failed" << endl;
        closesocket(listening);
        return -1;
    }

    if (listen(listening, SOMAXCONN) == SOCKET_ERROR) {
        cout << "Listen failed" << endl;
        closesocket(listening);
        return -1;
    }

    HANDLE hManagerThread = CreateThread(NULL, 0, Manager, &listening, 0, NULL);

    while (true) {
        SOCKET clientSocket = accept(listening, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "accept failed" << endl;
            WSACleanup();
            exit(1);
        }

        HANDLE hThread = CreateThread(NULL, 0, SocketHandler, reinterpret_cast<LPVOID>(clientSocket), 0, NULL);
        socketThreads.push_back(thread([hThread] {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
            }));
    }

    // Wait for the Manager thread to finish (in practice, you may want to implement a graceful shutdown mechanism)
    WaitForSingleObject(hManagerThread, INFINITE);
    CloseHandle(hManagerThread);

    closesocket(listening);
    WSACleanup();

    return 0;
}
