#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include "SocketHandler.cpp"
#pragma comment(lib, "ws2_32.lib")

#define SERVER_PORT 36

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr = { 0 };
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    SocketHandler* handler = nullptr;
    try {
        handler = new SocketHandler(listenSock);
    }
    catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    handler->startListening();

    // Asenkron iþlemleri yönetmek için olay döngüsü
    while (true) {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        LPOVERLAPPED overlapped;
        BOOL result = GetQueuedCompletionStatus(handler->getCompletionPort(), &bytesTransferred, &completionKey, &overlapped, INFINITE);

        if (result) {
            SocketHandler* handler = (SocketHandler*)completionKey;
            handler->handleCompletion(overlapped, bytesTransferred);
        }
        else {
            std::cerr << "GetQueuedCompletionStatus failed with error: " << GetLastError() << std::endl;
        }
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}
