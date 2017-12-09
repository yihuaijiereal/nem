#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "rdma/CompletionQueuePair.hpp"
#include "tcpWrapper.h"
#include "RDMAMessageBuffer.h"
#include <cstring>


using namespace std;
using namespace rdma;

int main(int argc, char **argv) {
    if (argc < 3 || (argv[1][0] == 'c' && argc < 4)) {
        cout << "Usage: " << argv[0] << " <client / server> <Port> <buffer_size> <k> <m> <w> <block_size> <message_num> [IP (if client)]" << endl;
        return -1;
    }
    const auto isClient = argv[1][0] == 'c';

    const auto port = ::atoi(argv[2]);
    const auto buffer_size = ::atoi(argv[3]);
    const auto code_k = ::atoi(argv[4]);
    const auto code_m = ::atoi(argv[5]);
    const auto code_w = ::atoi(argv[6]);
    const auto block_size = ::atoi(argv[7]);
    const auto message_num = ::atoi(argv[8]);

    cout<<"buffer size :"<<buffer_size<<endl;
    cout<<"k :"<<code_k<<endl;
    cout<<"m :"<<code_m<<endl;
    cout<<"w :"<<code_w<<endl;
    cout<<"block_size :"<<block_size<<endl;

    static const size_t MESSAGES = 1024 * 128;
    static const size_t BUFFERSIZE = 1024 * 512; // 16K

    if (isClient) {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, argv[9], &addr.sin_addr);

        auto sock = tcp_socket();
        tcp_connect(sock, addr);

        //auto sendData = array<uint8_t, 64>{"0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"};
        uint8_t *sendData;
        sendData =(uint8_t *)calloc(block_size*code_k,1);
        if(!sendData){
            cout << "alloc data error" << endl;
        }
        memset(sendData,'1',block_size*code_k);

        RDMAMessageBuffer rdma(buffer_size, sock);

        const auto start = chrono::steady_clock::now();
        for (size_t i = 0; i < message_num; ++i) {
            rdma.send(sendData, block_size*code_k, true);
            //cout<<"send"<<endl;
            auto answer = rdma.receive();
            //cout << answer.data() << endl;
        }
        const auto end = chrono::steady_clock::now();
        const auto msTaken = chrono::duration<double, milli>(end - start).count();
        const auto sTaken = msTaken / 1000;
        cout << message_num << " " << block_size * code_k << "B messages exchanged in " << msTaken << "ms" << endl;
        cout << message_num / sTaken << " msg/s" << endl;
        cout << "RTT:" << (msTaken * 1000) / message_num << " us" << endl;
    } else {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        auto sock = tcp_socket();
        tcp_bind(sock, addr);
        listen(sock, SOMAXCONN);
        sockaddr_in inAddr;

        auto acced = tcp_accept(sock, inAddr);

        RDMAMessageBuffer rdma(buffer_size, acced);

        for (size_t i = 0; i < message_num; ++i) {
            auto ping = rdma.receive();
            rdma.send(ping.data(), ping.size());
        }

        close(acced);
        close(sock);
    }
    return 0;
}

