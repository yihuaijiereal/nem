#include "ECBuffer.hpp"
#include <iostream>
#include "rdma/WorkRequest.hpp"
#include "tcpWrapper.h"
#include <infiniband/verbs.h>
#include <unistd.h>


using namespace std;
using namespace rdma;

static const size_t validity = 0xDEADDEADBEEFBEEF;

//--------------------------------------------------------------------------------
struct RmrInfo {
    uint32_t bufferKey;
    uint32_t readPosKey;
    uintptr_t bufferAddress;
    uintptr_t readPosAddress;
};

//--------------------------------------------------------------------------------
static void receiveAndSetupRmr(int sock, RemoteMemoryRegion &buffer, RemoteMemoryRegion &readPos) {
    RmrInfo rmrInfo{};
    tcp_read(sock, &rmrInfo, sizeof(rmrInfo));
    buffer.key = rmrInfo.bufferKey;
    buffer.address = rmrInfo.bufferAddress;
    readPos.key = rmrInfo.readPosKey;
    readPos.address = rmrInfo.readPosAddress;
}

//--------------------------------------------------------------------------------
static void sendRmrInfo(int sock, const MemoryRegion &buffer, const MemoryRegion &readPos) {
    RmrInfo rmrInfo{};
    rmrInfo.bufferKey = buffer.key->rkey;
    rmrInfo.bufferAddress = reinterpret_cast<uintptr_t>(buffer.address);
    rmrInfo.readPosKey = readPos.key->rkey;
    rmrInfo.readPosAddress = reinterpret_cast<uintptr_t>(readPos.address);
    tcp_write(sock, &rmrInfo, sizeof(rmrInfo));
}

//--------------------------------------------------------------------------------
static void exchangeQPNAndConnect(int sock, Network &network, QueuePair &queuePair) {
    Address addr{};
    addr.lid = network.getLID();
    addr.qpn = queuePair.getQPN();
    network.getsgid(addr);
    tcp_write(sock, &addr, sizeof(addr)); // Send own qpn to server
    tcp_read(sock, &addr, sizeof(addr)); // receive qpn
    queuePair.connect(addr);
}

//--------------------------------------------------------------------------------
vector<uint8_t> ECBuffer::receive() {
    //cout<<"come into receive "<<readPos<<endl;
    int received_blocks = 0;
    int received_blocks_num = 0;
    size_t receiveSize = 0;
    auto receiveValidity = static_cast<decltype(validity)>(0);
    char *failed_blocks = (char *)calloc(2*(k+m) - 1,sizeof(char));
    bool first_poll = true;
    int extra_data = -1;
    memset(failed_blocks,'1',2*(k+m)-1);
    vector<MemoryRegion::Slice> decodeData, decodeCode;

    while(received_blocks_num < k){
        for(int i=1;i<=k+m;++i){
            if(first_poll && (i != k+m))
                failed_blocks[2*i -1] = ',';
            readFromReceiveBuffer(readPos + i*(block_size + sizeof(validity))-sizeof(validity),
                    reinterpret_cast<uint8_t *>(&receiveValidity), sizeof(receiveValidity));
            if(receiveValidity == validity){
                if(!(received_blocks & (1<<i))){
                    received_blocks_num ++;
                    received_blocks |= (1<<i);
                    if(received_blocks_num<=k){
                        failed_blocks[2*(i-1)] = '0';
                    }
                }
            }
        }
        if(first_poll)
            first_poll = false;
        //cout<<received_blocks<<endl;
    }
    //cout<<received_blocks<<endl;
    //cout<<failed_blocks<<endl;

    if((received_blocks & blocks_all) != blocks_all){
       // cerr<<"need to decode"<<endl;
        for(int i=0;i<k;++i){
            auto beginPos = (readPos + (block_size + sizeof(validity))*i) & (size -1);
            if((beginPos + block_size) > size){
                //跨越结尾
                readFromReceiveBuffer(beginPos,decode_extra_buffer.get(),block_size);
                decodeData.push_back(decodeExtra.slice(0,block_size));
                extra_data = i;
            }else{
                decodeData.push_back(localReceive.slice(beginPos, block_size));
            }
        }

        for(int i=0;i<m;++i){
            auto beginPos = (readPos + (block_size + sizeof(validity))*(i+k)) & (size -1);
            if((beginPos + block_size) > size){
                //跨越结尾
                readFromReceiveBuffer(beginPos,decode_extra_buffer.get(),block_size);
                decodeCode.push_back(decodeExtra.slice(0,block_size));
            }else{
                decodeCode.push_back(localReceive.slice(beginPos,block_size));
            }
        }

        calc->decode(decodeData,decodeCode,failed_blocks);
    }

    free(failed_blocks);
    //cout<<"get enough blocks & decoded "<<readPos<<endl;
    //cout<<"received blocks "<<received_blocks<<"blocks all "<<blocks_all<<endl;

    if(extra_data>=0){//解码后的该数据块在额外的buffer里面，需要拷贝到receive buffer
        //cout<<"write back to receive buffer"<<endl;
        writeBackToReceiveBuffer(decode_extra_buffer.get(), block_size, (readPos + (block_size + sizeof(validity)) * extra_data) & (size - 1));
    }
    //usleep(1000);

    readFromReceiveBuffer(readPos, reinterpret_cast<uint8_t *>(&receiveSize), sizeof(receiveSize));
    //cout<<readPos<<endl;
    //cout<<receiveSize<<endl;

    auto result = vector<uint8_t>(receiveSize);
    size_t receiveSize_save = receiveSize;

    //cout<<"about to retrive block 1 "<<readPos<<endl;
    if(receiveSize <= block_size - sizeof(receiveSize)){
        readFromReceiveBuffer(readPos + sizeof(receiveSize), result.data(), receiveSize);
        receiveSize = 0;
    }else{
        readFromReceiveBuffer(readPos + sizeof(receiveSize), result.data(), block_size - sizeof(receiveSize));
        receiveSize -= (block_size - sizeof(receiveSize));
    }

    //cout<<"about to retrive other blocks "<<readPos<<endl;
    int i = 1;
    while(receiveSize > 0){
        if(receiveSize <= block_size){
            readFromReceiveBuffer(readPos + (sizeof(validity)+block_size)*i, result.data() + receiveSize_save - receiveSize, receiveSize);
            receiveSize = 0;
        }else{
            readFromReceiveBuffer(readPos + (sizeof(validity)+block_size)*i, result.data() + receiveSize_save - receiveSize, block_size);
            receiveSize -= block_size;
        }
        ++i;
    }
    //cout<<"about to zero biffers "<<readPos<<endl;
    zeroReceiveBuffer(readPos, (sizeof(validity) + block_size) * (k + m));

    readPos += (sizeof(validity) + block_size) * (k + m);
    //cout<<"about to leave "<<readPos<<endl;

    return result;
}

/*
//--------------------------------------------------------------------------------
size_t ECBuffer::receive(void *whereTo, size_t maxSize) {
    size_t receiveSize = 0;
    auto receiveValidity = static_cast<decltype(validity)>(0);
    do {
        readFromReceiveBuffer(readPos, reinterpret_cast<uint8_t *>(&receiveSize), sizeof(receiveSize));
        readFromReceiveBuffer(readPos + sizeof(receiveSize) + receiveSize,
                              reinterpret_cast<uint8_t *>(&receiveValidity),
                              sizeof(receiveValidity));
    } while (receiveValidity != validity);

    if (receiveSize > maxSize) {
        throw runtime_error{"plz only read whole messages for now!"}; // probably buffer partially read msgs
    }
    readFromReceiveBuffer(readPos + sizeof(receiveSize), reinterpret_cast<uint8_t *>(whereTo), receiveSize);
    zeroReceiveBuffer(readPos, sizeof(receiveSize) + receiveSize + sizeof(validity));

    readPos += sizeof(receiveSize) + receiveSize + sizeof(validity);

    return receiveSize;
}
*/

//--------------------------------------------------------------------------------


//--------------------------------------------------------------------------------
ECBuffer::ECBuffer(size_t size, int sock, int k, int m, int w, int block_size) :
    size(size),
    net(sock, k+m),
    k(k),
    m(m),
    w(w),
    block_size(block_size),
    receiveBuffer(make_unique<volatile uint8_t[]>(size)),
    sendBuffer(make_unique<uint8_t[]>(size)),
    encode_extra_buffer(make_unique<uint8_t[]>(block_size)),
    decode_extra_buffer(make_unique<uint8_t[]>(block_size)),
    localSend(sendBuffer.get(), size, net.network.getProtectionDomain(), MemoryRegion::Permission::LocalWrite),
    localReceive(const_cast<uint8_t *>(receiveBuffer.get()), size, net.network.getProtectionDomain(),
                     MemoryRegion::Permission::LocalWrite | MemoryRegion::Permission::RemoteWrite),
    encodeExtra(const_cast<uint8_t *>(encode_extra_buffer.get()), block_size, net.network.getProtectionDomain(),
                      MemoryRegion::Permission::LocalWrite),
    decodeExtra(const_cast<uint8_t *>(decode_extra_buffer.get()),block_size, net.network.getProtectionDomain(),
                      MemoryRegion::Permission::LocalWrite),
    localReadPos(&readPos, sizeof(readPos), net.network.getProtectionDomain(),
                     MemoryRegion::Permission::RemoteRead),
    localCurrentRemoteReceive(const_cast<size_t *>(&currentRemoteReceive), sizeof(currentRemoteReceive),
                     net.network.getProtectionDomain(), MemoryRegion::Permission::LocalWrite)
{
    const bool powerOfTwo = (size != 0) && !(size & (size - 1));
    if (not powerOfTwo) {
        throw runtime_error{"size should be a power of 2"};
    }

    //用来判断是否所有的数据块都收到
    for(int i=1;i<=k;++i){
        blocks_all |= (1<<i);
    }

    tcp_setBlocking(sock); // just set the socket to block for our setup.

    sendRmrInfo(sock, localReceive, localReadPos);
    receiveAndSetupRmr(sock, remoteReceive, remoteReadPos);
    
    calc = make_unique<rdma::ECCalc>(net.network,k,m,w,block_size*k);
}
//--------------------------------------------------------------------------------
template<typename Func>
void wraparound(const size_t totalSize, const size_t todoSize, const size_t pos, Func &&func) {
    const size_t beginPos = pos & (totalSize - 1);
    if ((totalSize - beginPos) >= todoSize) {
        func(0, beginPos, beginPos + todoSize);
    } else {
        const auto fst = beginPos;
        const auto fstToRead = totalSize - beginPos;
        const auto snd = 0;
        const auto sndToRead = todoSize - fstToRead;
        func(0, fst, fst + fstToRead);
        func(fstToRead, snd, snd + sndToRead);
    }
}

//--------------------------------------------------------------------------------
template<typename T, typename Func>
void wraparound(T *buffer, const size_t totalSize, const size_t todoSize, const size_t pos, Func &&func) {
    wraparound(totalSize, todoSize, pos, [&](auto prevBytes, auto beginPos, auto endPos) {
        func(prevBytes, buffer + beginPos, buffer + endPos);
    });
}

//--------------------------------------------------------------------------------
void ECBuffer::send(const uint8_t *data, size_t length) {
    send(data, length, true);
}

void ECBuffer::writeToSendExtra(size_t pos){
    const size_t beginPos = pos & (size - 1);
    const auto fstSize = size - beginPos;

    const auto prev = sendBuffer.get();
    const auto dest = encode_extra_buffer.get();

    copy(prev + beginPos, prev + size,  dest);
    copy(prev, prev + block_size - fstSize, dest + fstSize);
}

//--------------------------------------------------------------------------------
void ECBuffer::send(const uint8_t *data, size_t length, bool inln) {
    //cout<<"come into send "<<sendPos<<endl;
    vector<MemoryRegion::Slice> encodeData, encodeCode;
    bool extra = false;
    int parityExtra = -1;
    const size_t c_sizeToWrite = sizeof(length) + length;

    size_t sizeToWrite = c_sizeToWrite;
    if (sizeToWrite > block_size * k) throw runtime_error{"data > framesize!"};

    //发送第一个数据块
    const size_t startOfWrite_save = sendPos;
    size_t startOfWrite = startOfWrite_save;

    if(((sendPos + block_size) & (size-1)) < block_size)
        extra = true;
    if(sizeToWrite <= block_size){
        writeToSendBuffer(reinterpret_cast<const uint8_t *>(&length), sizeof(length));
        writeToSendBuffer(data, length);
        zeroSendBuffer(block_size - sizeToWrite);
        writeToSendBuffer(reinterpret_cast<const uint8_t *>(&validity), sizeof(validity));
    }else{
        writeToSendBuffer(reinterpret_cast<const uint8_t *>(&length), sizeof(length));
        writeToSendBuffer(data, block_size - sizeof(length));
        writeToSendBuffer(reinterpret_cast<const uint8_t *>(&validity), sizeof(validity));
    }
    if((message_number%100)>4){
    wraparound(size, block_size + sizeof(validity), startOfWrite, [&](auto, auto beginPos, auto endPos) {
        const auto sendSlice = localSend.slice(beginPos, endPos - beginPos);
        const auto remoteSlice = remoteReceive.slice(beginPos);
        WriteWorkRequestBuilder(sendSlice, remoteSlice, false)
                .setInline(inln && sendSlice.size <= net.queuePairs[0]->getMaxInlineSize())
                .send(*net.queuePairs[0]);
    });
    }
    

    if(extra){
        //
        writeToSendExtra(startOfWrite);
        encodeData.push_back(encodeExtra.slice(0,block_size));
        extra = false;
    }else{
        encodeData.push_back(localSend.slice(startOfWrite & (size - 1) , block_size));//????
    }
    startOfWrite = startOfWrite + block_size + sizeof(validity);
    sizeToWrite = sizeToWrite > block_size ? sizeToWrite - block_size : 0;
    

    //发送剩余的数据块
    for(int i=1;i<k;++i){
        if(((sendPos + block_size) & (size-1)) < block_size)
            extra = true;
        if(sizeToWrite <= block_size){
            writeToSendBuffer(data + length - sizeToWrite , sizeToWrite);
            zeroSendBuffer(block_size - sizeToWrite);
            writeToSendBuffer(reinterpret_cast<const uint8_t *>(&validity), sizeof(validity));
        }else{
            writeToSendBuffer(data + length - sizeToWrite, block_size);//???
            writeToSendBuffer(reinterpret_cast<const uint8_t *>(&validity), sizeof(validity));

        }
        //if(i != 3){
        wraparound(size, block_size + sizeof(validity), startOfWrite, [&](auto, auto beginPos, auto endPos) {
            const auto sendSlice = localSend.slice(beginPos, endPos - beginPos);
            const auto remoteSlice = remoteReceive.slice(beginPos);
            WriteWorkRequestBuilder(sendSlice, remoteSlice, false)
                    .setInline(inln && sendSlice.size <= net.queuePairs[i]->getMaxInlineSize())
                    .send(*net.queuePairs[i]);
        });
        //}
        if(extra){
            //
            writeToSendExtra(startOfWrite);
            encodeData.push_back(encodeExtra.slice(0,block_size));
            extra = false;
        }else{
            encodeData.push_back(localSend.slice(startOfWrite & (size - 1), block_size));
        }
        startOfWrite = startOfWrite + block_size + sizeof(validity);
        sizeToWrite = (sizeToWrite > block_size) ? sizeToWrite - block_size : 0;
    }
    
    //todo,准备校验块校验块
    for(int i=0;i<m;++i){
        if(((sendPos + block_size) & (size-1)) < block_size)
            extra = true;

        zeroSendBuffer(block_size);
        writeToSendBuffer(reinterpret_cast<const uint8_t *>(&validity), sizeof(validity));
        if(extra){
            //
            writeToSendExtra(startOfWrite);
            encodeCode.push_back(encodeExtra.slice(0,block_size));
            
            extra = false;
            parityExtra = i;
        }else{
            encodeCode.push_back(localSend.slice(startOfWrite & (size - 1), block_size));
        }
        startOfWrite = startOfWrite + block_size + sizeof(validity);
    }

    if((message_number % 100)<=4){    
    calc->encode(encodeData,encodeCode);
    
    if(parityExtra >= 0){
        sendPos = startOfWrite_save + (k + parityExtra) * (block_size + sizeof(validity));
        writeToSendBuffer(encode_extra_buffer.get(),block_size);//??????????????
        sendPos = startOfWrite_save + (k + m) * (block_size + sizeof(validity));
    }
    
    //发送校验块
    for(int i=0;i<m;++i){
        startOfWrite = startOfWrite_save + (k + i) * (block_size + sizeof(validity));
        wraparound(size, block_size + sizeof(validity), startOfWrite, [&](auto, auto beginPos, auto endPos) {
            const auto sendSlice = localSend.slice(beginPos, endPos - beginPos);
            const auto remoteSlice = remoteReceive.slice(beginPos);
            WriteWorkRequestBuilder(sendSlice, remoteSlice, false)
                    .setInline(inln && sendSlice.size <= net.queuePairs[i+k]->getMaxInlineSize())
                    .send(*net.queuePairs[i+k]);
        });
    }
    //cout<<"about to leave "<<sendPos<<endl;
    }
    message_number++;

}

//--------------------------------------------------------------------------------
void ECBuffer::writeToSendBuffer(const uint8_t *data, size_t sizeToWrite) {
    // Make sure, there is enough space
    size_t safeToWrite = size - (sendPos - currentRemoteReceive);
    while (sizeToWrite > safeToWrite) {
        ReadWorkRequestBuilder(localCurrentRemoteReceive, remoteReadPos, true)
                .send(*net.queuePairs[read_qp]);
        while (net.completionQueue.pollSendCompletionQueue() !=
               ReadWorkRequest::getId()); // Poll until read has finished
        safeToWrite = size - (sendPos - currentRemoteReceive);
        read_qp = (read_qp + 1)%(k+m);
    }

    wraparound(sendBuffer.get(), size, sizeToWrite, sendPos, [&](auto prevBytes, auto begin, auto end) {
        copy(data + prevBytes, data + prevBytes + distance(begin, end), begin);
    });

    sendPos += sizeToWrite;
}

void ECBuffer::writeBackToReceiveBuffer(const uint8_t *data, size_t sizeToWrite, size_t beginPos) {
    wraparound(receiveBuffer.get(), size, sizeToWrite, beginPos, [&](auto prevBytes, auto begin, auto end) {
        copy(data + prevBytes, data + prevBytes + distance(begin, end), begin);
    });
}

//--------------------------------------------------------------------------------
void ECBuffer::readFromReceiveBuffer(size_t readPos, uint8_t *whereTo, size_t sizeToRead) const {
    wraparound(receiveBuffer.get(), size, sizeToRead, readPos, [whereTo](auto prevBytes, auto begin, auto end) {
        copy(begin, end, whereTo + prevBytes);
    });
    // Don't increment currentRead, we might need to read the same position multiple times!
}

//--------------------------------------------------------------------------------
void ECBuffer::zeroReceiveBuffer(size_t beginReceiveCount, size_t sizeToZero) {
    wraparound(receiveBuffer.get(), size, sizeToZero, beginReceiveCount, [](auto, auto begin, auto end) {
        fill(begin, end, 0);
    });
}

//--------------------------------------------------------------------------------
void ECBuffer::zeroSendBuffer(size_t sizeToZero) {
    size_t safeToWrite = size - (sendPos - currentRemoteReceive);
    while (sizeToZero > safeToWrite) {
        ReadWorkRequestBuilder(localCurrentRemoteReceive, remoteReadPos, true)
                .send(*net.queuePairs[read_qp]);
        while (net.completionQueue.pollSendCompletionQueue() !=
               ReadWorkRequest::getId()); // Poll until read has finished
        safeToWrite = size - (sendPos - currentRemoteReceive);
        read_qp = (read_qp + 1)%(k+m);
    }
    wraparound(sendBuffer.get(), size, sizeToZero, sendPos, [](auto, auto begin, auto end) {
        fill(begin, end, '0');
    });

    sendPos += sizeToZero;
}


//--------------------------------------------------------------------------------
bool ECBuffer::hasData() const {
    size_t receiveSize;
    auto receiveValidity = static_cast<decltype(validity)>(0);
    readFromReceiveBuffer(readPos, reinterpret_cast<uint8_t *>(&receiveSize), sizeof(receiveSize));
    readFromReceiveBuffer(readPos + sizeof(receiveSize) + receiveSize, reinterpret_cast<uint8_t *>(&receiveValidity),
                          sizeof(receiveValidity));
    return (receiveValidity == validity);
}

//--------------------------------------------------------------------------------
ECBuffer::~ECBuffer(){
    calc.reset();
}

//--------------------------------------------------------------------------------
RDMANetworking::RDMANetworking(int sock, int qp_num) :
        qp_num(qp_num),
        completionQueue(network)
{
    queuePairs = (QueuePair**)calloc(qp_num, sizeof(*queuePairs));

    for(int i=0;i<qp_num;++i){
        queuePairs[i] = new QueuePair(network, completionQueue);
        
        tcp_setBlocking(sock);
        exchangeQPNAndConnect(sock, network, *queuePairs[i]);
    }
    //printf("context addr in RDMANetworking : %lld\n",network.get_context());
    
}

RDMANetworking::~RDMANetworking()
{
    for(int i=0;i<qp_num;++i){
        delete queuePairs[i];
    }
    free(queuePairs);
}

//--------------------------------------------------------------------------------
