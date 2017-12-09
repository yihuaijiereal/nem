#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "rdma/CompletionQueuePair.hpp"
#include "tcpWrapper.h"
#include "ECBuffer.hpp"


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


    static const size_t MESSAGES = 10000;
    static const size_t BUFFERSIZE = 1024 * 1024; // 16K

    if (isClient) {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, argv[9], &addr.sin_addr);

        auto sock = tcp_socket();
        tcp_connect(sock, addr);

        auto sendData = array<uint8_t, 320>{"0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"};
        ECBuffer rdma(buffer_size, sock, code_k, code_m, code_w, block_size);

        const auto start = chrono::steady_clock::now();
        for (size_t i = 0; i < message_num; ++i) {
            //std::cout<<"send message "<<i<<" size "<<sendData.size()<<std::endl;
            //std::cout<<"before:  "<<sendData.data()<<std::endl;
            rdma.send(sendData.data(), sendData.size(), true);
            //std::cout<<"after:  "<<sendData.data()<<std::endl;
            auto answer = rdma.receive();
            //std::cout<<answer.data()<<std::endl;
            if (answer.size() != sendData.size()) {
                throw runtime_error{"answer has wrong size!"};
            }
        }
        const auto end = chrono::steady_clock::now();
        const auto msTaken = chrono::duration<double, milli>(end - start).count();
        const auto sTaken = msTaken / 1000;
        cout << message_num << " " << block_size*code_k << "B messages exchanged in " << msTaken << "ms" << endl;
        cout << message_num / sTaken << " msg/s" << endl;
        cout << "RTT:" << (msTaken * 1000) / message_num << " us" << endl;
        //sleep(1);
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

        ECBuffer rdma(buffer_size, acced, code_k, code_m, code_w, block_size);

        for (size_t i = 0; i < message_num; ++i) {
            auto ping = rdma.receive();
            //std::cout<<ping.data()<<std::endl;
            //std::cout<<"send message "<<i<<" size "<<ping.size()<<std::endl;
            //std::cout<<ping.data()<<std::endl;
            rdma.send(ping.data(), ping.size());
        }
        //sleep(1);

        close(acced);
        close(sock);
    }
    return 0;
}

