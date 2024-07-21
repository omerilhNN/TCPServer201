#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <iostream>
#include <string>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

#define BUFFER_SIZE 512

class SocketHandler {
public:
    SocketHandler(SOCKET listenSocket) : listenSock(listenSocket), acceptExFunc(nullptr) {
        ZeroMemory(&overlappedAccept, sizeof(OVERLAPPED));
        overlappedAccept.hEvent = this;

        // AcceptEx iþlevini yükle
        DWORD bytesReturned;
        GUID acceptExGuid = WSAID_ACCEPTEX;
        WSAIoctl(listenSock, SIO_GET_EXTENSION_FUNCTION_POINTER, &acceptExGuid, sizeof(acceptExGuid),
            &acceptExFunc, sizeof(acceptExFunc), &bytesReturned, NULL, NULL);

        // Completion port oluþtur ve listen socket ekle
        completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (!completionPort) {
            std::cerr << "CreateIoCompletionPort failed with error: " << GetLastError() << std::endl;
            throw std::runtime_error("CreateIoCompletionPort failed");
        }

        if (!CreateIoCompletionPort((HANDLE)listenSock, completionPort, (ULONG_PTR)this, 0)) {
            std::cerr << "CreateIoCompletionPort failed with error: " << GetLastError() << std::endl;
            throw std::runtime_error("CreateIoCompletionPort failed");
        }
    }

    void startListening() {
        if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
            throw std::runtime_error("Listen failed");
        }
        startAccepting();
    }

    HANDLE getCompletionPort() {
        return completionPort;
    }

    void handleCompletion(LPOVERLAPPED overlapped, DWORD bytesTransferred) {
        if (overlapped == &overlappedAccept) {
            onAcceptComplete(0);
        }
        else if (overlapped == &overlappedRecv) {
            onReceiveComplete(0, bytesTransferred);
        }
        else if (overlapped == &overlappedSend) {
            onSendComplete(0, bytesTransferred);
        }
    }

    static void CALLBACK AcceptCompletionRoutine(DWORD dwError, DWORD cbTransferred, LPWSAOVERLAPPED lpOverlapped, DWORD dwFlags) {
        SocketHandler* handler = (SocketHandler*)lpOverlapped->hEvent;
        handler->onAcceptComplete(dwError);
    }

    static void CALLBACK RecvCompletionRoutine(DWORD dwError, DWORD cbTransferred, LPWSAOVERLAPPED lpOverlapped, DWORD dwFlags) {
        SocketHandler* handler = (SocketHandler*)lpOverlapped->hEvent;
        handler->onReceiveComplete(dwError, cbTransferred);
    }

    static void CALLBACK SendCompletionRoutine(DWORD dwError, DWORD cbTransferred, LPWSAOVERLAPPED lpOverlapped, DWORD dwFlags) {
        SocketHandler* handler = (SocketHandler*)lpOverlapped->hEvent;
        handler->onSendComplete(dwError, cbTransferred);
    }

private:
    SOCKET listenSock;
    SOCKET clientSock;
    LPFN_ACCEPTEX acceptExFunc;
    HANDLE completionPort;
    char acceptBuffer[2 * (sizeof(sockaddr_in) + 16)];
    char recvBuffer[BUFFER_SIZE];
    char sendBuffer[BUFFER_SIZE];
    WSABUF wsaBufRecv;
    WSABUF wsaBufSend;
    OVERLAPPED overlappedAccept;
    OVERLAPPED overlappedRecv;
    OVERLAPPED overlappedSend;

    void startAccepting() {
        clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSock == INVALID_SOCKET) {
            std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
            return;
        }

        int result = acceptExFunc(listenSock, clientSock, acceptBuffer, 0,
            sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16,
            NULL, &overlappedAccept);
        if (result == FALSE && WSAGetLastError() != WSA_IO_PENDING) {
            std::cerr << "AcceptEx failed with error: " << WSAGetLastError() << std::endl;
            closesocket(clientSock);
            return;
        }
    }

    void onAcceptComplete(DWORD dwError) {
        if (dwError == 0) {
            std::cout << "Client connected." << std::endl;

            // Client socket için recv overlapped yapýsýný hazýrla
            ZeroMemory(&overlappedRecv, sizeof(OVERLAPPED));
            overlappedRecv.hEvent = this;
            wsaBufRecv.buf = recvBuffer;
            wsaBufRecv.len = BUFFER_SIZE;

            // Ýlk veri alýmýný baþlat
            int result = WSARecv(clientSock, &wsaBufRecv, 1, NULL, NULL, &overlappedRecv, RecvCompletionRoutine);
            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                std::cerr << "WSARecv failed with error: " << WSAGetLastError() << std::endl;
                closesocket(clientSock);
                startAccepting();
                return;
            }

            // Client socket'i completion port'a ekle
            if (!CreateIoCompletionPort((HANDLE)clientSock, completionPort, (ULONG_PTR)this, 0)) {
                std::cerr << "CreateIoCompletionPort failed with error: " << GetLastError() << std::endl;
                closesocket(clientSock);
                return;
            }
        }
        else {
            std::cerr << "AcceptEx failed with error: " << dwError << std::endl;
            closesocket(clientSock);
        }

        // Yeni baðlantýlar için kabul iþlemini tekrar baþlat
        startAccepting();
    }

    void onReceiveComplete(DWORD dwError, DWORD cbTransferred) {
        if (dwError == 0 && cbTransferred > 0) {
            recvBuffer[cbTransferred] = '\0';
            std::cout << "Received from client: " << recvBuffer << std::endl;

            // Gelen veriyi iþleme (örneðin toplama iþlemi)
            std::string data(recvBuffer);
            size_t pos1 = data.find(',');
            size_t pos2 = data.find(',', pos1 + 1);

            if (pos1 != std::string::npos && pos2 != std::string::npos) {
                int val1 = std::stoi(data.substr(0, pos1));
                int val2 = std::stoi(data.substr(pos1 + 1, pos2 - pos1 - 1));
                char op = data[pos2 + 1];

                int result = 0;
                if (op == '+') {
                    result = val1 + val2;
                }
                // Daha fazla iþlem eklenebilir

                std::string response = "Result: " + std::to_string(result);
                strcpy(sendBuffer, response.c_str());
                wsaBufSend.buf = sendBuffer;
                wsaBufSend.len = response.length();

                // Yanýt gönderme
                int result = WSASend(clientSock, &wsaBufSend, 1, NULL, 0, &overlappedSend, SendCompletionRoutine);
                if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                    std::cerr << "WSASend failed with error: " << WSAGetLastError() << std::endl;
                    closesocket(clientSock);
                    return;
                }
            }
        }
        else {
            std::cerr << "Receive failed with error: " << dwError << std::endl;
            closesocket(clientSock);
        }
    }

    void onSendComplete(DWORD dwError, DWORD cbTransferred) {
        if (dwError == 0) {
            std::cout << "Send completed successfully. Bytes sent: " << cbTransferred << std::endl;

            // Yeni bir veri almak için hazýrlýk
            ZeroMemory(&overlappedRecv, sizeof(OVERLAPPED));
            overlappedRecv.hEvent = this;
            wsaBufRecv.buf = recvBuffer;
            wsaBufRecv.len = BUFFER_SIZE;

            int result = WSARecv(clientSock, &wsaBufRecv, 1, NULL, NULL, &overlappedRecv, RecvCompletionRoutine);
            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
                std::cerr << "WSARecv failed with error: " << WSAGetLastError() << std::endl;
                closesocket(clientSock);
            }
        }
        else {
            std::cerr << "Send failed with error: " << dwError << std::endl;
            closesocket(clientSock);
        }
    }
};
