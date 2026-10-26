#include "mpi/protocol.hpp"
#include "mpi/counts.hpp"
#include <sys/socket.h>
#include <poll.h>
#include <cerrno>
#include <stdexcept>
#include <algorithm>

namespace conway::mpi::wire {
void put(Bytes& out, std::uint64_t value) {
    for (int shift=56; shift>=0; shift-=8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
std::uint64_t get(const Bytes& in, std::size_t& offset) {
    if (offset > in.size() || in.size()-offset < 8) throw std::runtime_error("MPI protocol: truncated integer");
    std::uint64_t value=0;
    for (int i=0; i<8; ++i) value=(value<<8)|in[offset++];
    return value;
}
namespace {
void transfer(int fd, std::uint8_t* data, std::size_t length, bool writing,
              Deadline deadline, const std::function<void()>& tick) {
    while (length) {
        if (tick) tick();
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("MPI protocol: timeout");
        pollfd p{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
        const int ready = poll(&p, 1, 10);
        if (ready<0 && errno==EINTR) continue;
        if (ready<0) throw std::runtime_error("MPI protocol: poll failed");
        if (!ready) continue;
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("MPI protocol: timeout");
        const auto n = writing ? ::send(fd,data,length,MSG_NOSIGNAL|MSG_DONTWAIT)
                               : ::recv(fd,data,length,MSG_DONTWAIT);
        if (n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)) continue;
        if (n<=0) throw std::runtime_error("MPI protocol: disconnected");
        data+=n; length-=static_cast<std::size_t>(n);
    }
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("MPI protocol: timeout");
}
}
void send(int fd, const Frame& f, Deadline d, const std::function<void()>& tick) {
    if (f.payload.size()>maxPayload) throw std::runtime_error("MPI protocol: payload too large");
    Bytes header;
    for (auto v : {std::uint64_t{0x434f4e5741594d50},version,f.type,f.session,f.request,std::uint64_t(f.payload.size())}) put(header,v);
    transfer(fd,header.data(),header.size(),true,d,tick);
    // transfer never mutates a sending buffer.
    transfer(fd,const_cast<std::uint8_t*>(f.payload.data()),f.payload.size(),true,d,tick);
}
Frame receive(int fd, Deadline d, const std::function<void()>& tick, const HeaderCheck& check) {
    Bytes header(48);
    transfer(fd,header.data(),header.size(),false,d,tick);
    std::size_t offset=0;
    if (get(header,offset)!=0x434f4e5741594d50 || get(header,offset)!=version)
        throw std::runtime_error("MPI protocol: incompatible header");
    Frame f;
    f.type=get(header,offset); f.session=get(header,offset); f.request=get(header,offset);
    const auto length=get(header,offset);
    if (length>maxPayload) throw std::runtime_error("MPI protocol: payload too large");
    if (f.type<Ready || f.type>BatchResult || !f.session ||
        (f.type==Ready ? f.request!=0 : f.request==0))
        throw std::runtime_error("MPI protocol: invalid type or identity");
    const bool validLength =
        (f.type==Ready && length>=16 && length<=8*1025 && length%8==0) ||
        (f.type==Load && length>=25) || (f.type==Advance && length==8) || (f.type==RunBatch && length==16) ||
        (f.type==BatchResult && length>=32 && length<=32+8192) ||
        ((f.type==Snapshot || f.type==Close || f.type==Closed) && length==0) ||
        (f.type==Reply && (length==0 || length>=33)) || (f.type==Error && length<=4096);
    if(!validLength) throw std::runtime_error("MPI protocol: invalid message length");
    if(check) check(f,length);
    // Read only the fixed board metadata before accepting its bulk allocation.
    // Nonempty REPLY is a snapshot in v1; other acknowledgements are empty.
    const auto prefixLength=f.type==Load ? 24u : f.type==Reply && length ? 32u : 0u;
    Bytes prefix(prefixLength);
    transfer(fd,prefix.data(),prefix.size(),false,d,tick);
    if(prefixLength) {
        std::size_t pos=0;
        const auto width=get(prefix,pos),height=get(prefix,pos),generation=get(prefix,pos);
        if(width>SIZE_MAX || height>SIZE_MAX || generation>SIZE_MAX)
            throw std::runtime_error("MPI protocol: board metadata overflow");
        const auto count=conway::mpi::checked_cell_count(width,height);
        if(count>maxPayload-32 || count!=length-prefixLength ||
            (f.type==Reply && get(prefix,pos)>count))
            throw std::runtime_error("MPI protocol: board metadata/length mismatch");
    }
    f.payload.resize(static_cast<std::size_t>(length));
    std::copy(prefix.begin(),prefix.end(),f.payload.begin());
    auto* remaining=f.payload.data();
    if(prefixLength) remaining+=prefixLength;
    transfer(fd,remaining,f.payload.size()-prefixLength,false,d,tick);
    return f;
}
} // namespace conway::mpi::wire
