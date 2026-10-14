#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t MAGIC = 0x46414252u; // "FABR"
constexpr std::size_t HEADER_SIZE = 20;
constexpr std::size_t MAX_BODY = 1u << 20;
std::atomic<bool> running{true};

struct Socket {
  int fd{-1};
  explicit Socket(int v = -1) : fd(v) {}
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& other) noexcept : fd(other.fd) { other.fd = -1; }
  ~Socket() { if (fd >= 0) ::close(fd); }
};

void fail(const std::string& why) { throw std::runtime_error(why); }
std::string option(int argc, char** argv, const std::string& key, const std::string& def) {
  for (int i=2; i<argc; ++i) {
    if (argv[i] == key) { if (i+1 == argc) fail("missing value for "+key); return argv[i+1]; }
  }
  return def;
}
std::uint32_t number(const std::string& arg, std::uint32_t min, std::uint32_t max) {
  if(arg.empty()) fail("empty number");
  std::size_t used=0;
  unsigned long long v=0;
  try { v=std::stoull(arg,&used); } catch (...) { fail("invalid number: "+arg); }
  if(used!=arg.size() || arg[0]=='-' || v<min || v>max) fail("number out of range: "+arg);
  return static_cast<std::uint32_t>(v);
}

std::uint32_t crc32(const unsigned char* data, std::size_t len) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for(std::size_t i=0;i<len;++i){ crc ^= data[i]; for(int k=0;k<8;++k) crc=(crc>>1) ^ ((crc&1u)?0xEDB88320u:0u); }
  return ~crc;
}
void set32(unsigned char* b, std::uint32_t x){ x=htonl(x); std::memcpy(b,&x,4); }
std::uint32_t get32(const unsigned char* b){ std::uint32_t x=0; std::memcpy(&x,b,4); return ntohl(x); }
void write_all(int fd,const unsigned char* b,std::size_t len) {
  while(len){ ssize_t n=::send(fd,b,len,MSG_NOSIGNAL); if(n<0 && errno==EINTR) continue; if(n<=0) fail("send error/closed"); b+=n; len-=static_cast<std::size_t>(n); }
}
bool read_all(int fd,unsigned char* b,std::size_t len) {
  std::size_t got=0;
  while(got<len){ ssize_t n=::recv(fd,b+got,len-got,0); if(n<0 && errno==EINTR) continue; if(n==0 && got==0) return false; if(n<=0) fail("truncated TCP frame or recv failed"); got+=static_cast<std::size_t>(n); }
  return true;
}
struct Frame {std::uint32_t seq; std::vector<unsigned char> payload;};
std::vector<unsigned char> encode(std::uint32_t seq,const std::vector<unsigned char>& payload) {
  if(payload.size()>MAX_BODY) fail("payload too big");
  std::vector<unsigned char> b(HEADER_SIZE+payload.size(),0);
  set32(b.data(),MAGIC); set32(b.data()+4,1); set32(b.data()+8,seq);
  set32(b.data()+12,static_cast<std::uint32_t>(payload.size()));
  set32(b.data()+16,crc32(payload.data(),payload.size()));
  std::copy(payload.begin(),payload.end(),b.begin()+static_cast<std::ptrdiff_t>(HEADER_SIZE));
  return b;
}
bool read_frame(int fd,Frame& frame) {
  unsigned char h[HEADER_SIZE];
  if(!read_all(fd,h,HEADER_SIZE)) return false;
  if(get32(h)!=MAGIC || get32(h+4)!=1) fail("bad magic/version");
  const auto len=get32(h+12);
  if(len>MAX_BODY) fail("oversized packet");
  frame.seq=get32(h+8); frame.payload.resize(len);
  if(len && !read_all(fd,frame.payload.data(),len)) fail("truncated payload");
  if(crc32(frame.payload.data(),frame.payload.size())!=get32(h+16)) fail("bad payload CRC");
  return true;
}
void timeout(int fd) {
  timeval tv{3,0};
  if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv))<0 || setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv))<0) fail("set timeout failed");
}
Socket connect_to(const std::string& ip,std::uint16_t port) {
  Socket sock(::socket(AF_INET,SOCK_STREAM,0)); if(sock.fd<0) fail("socket failed");
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port);
  if(inet_pton(AF_INET,ip.c_str(),&a.sin_addr)!=1) fail("invalid IPv4 address");
  if(::connect(sock.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0) fail("connect failed");
  timeout(sock.fd); return sock;
}
Socket listener(const std::string& ip,std::uint16_t port,std::uint16_t& bound) {
  Socket s(::socket(AF_INET,SOCK_STREAM,0)); if(s.fd<0) fail("socket failed");
  int yes=1; (void)setsockopt(s.fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port);
  if(inet_pton(AF_INET,ip.c_str(),&a.sin_addr)!=1) fail("invalid bind IP");
  if(bind(s.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0 || listen(s.fd,32)!=0) fail("bind/listen failed");
  socklen_t sz=sizeof(a); if(getsockname(s.fd,reinterpret_cast<sockaddr*>(&a),&sz)!=0) fail("getsockname failed");
  bound=ntohs(a.sin_port); return s;
}

void serve_connection(int fd,std::uint32_t delay_us,std::uint32_t corrupt_every) {
  Socket owned(fd);
  try {
    timeout(fd);
    Frame incoming{};
    std::uint64_t n=0;
    while(read_frame(fd,incoming)){
      ++n;
      if(delay_us) std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
      auto outgoing=encode(incoming.seq,incoming.payload);
      if(corrupt_every && n%corrupt_every==0 && !incoming.payload.empty()) outgoing.back()^=0x01;
      write_all(fd,outgoing.data(),outgoing.size());
    }
  }catch(const std::exception& e) { std::cerr << "connection closed: "<<e.what()<<'\n'; }
}
int server(int argc,char** argv) {
  const std::string ip=option(argc,argv,"--bind","127.0.0.1");
  auto port=number(option(argc,argv,"--port","0"),0,65535);
  const auto delay=number(option(argc,argv,"--delay-us","0"),0,1000000);
  const auto corrupt=number(option(argc,argv,"--corrupt-every","0"),0,1000000);
  const auto max_clients=number(option(argc,argv,"--max-clients","1"),1,10000);
  std::uint16_t bound=0;
  auto s=listener(ip,static_cast<std::uint16_t>(port),bound);
  std::cout << "READY tcp=" << ip << ':' << bound << std::endl;
  std::vector<std::thread> threads;
  for(std::uint32_t i=0;i<max_clients;++i){
    int fd=::accept(s.fd,nullptr,nullptr);
    if(fd<0) fail("accept failed");
    threads.emplace_back([fd,delay,corrupt]{serve_connection(fd,delay,corrupt);});
  }
  for(auto& t:threads) t.join();
  return 0;
}
std::vector<unsigned char> payload_data(std::size_t bytes,std::uint32_t seq) {
  std::vector<unsigned char> out(bytes);
  for(std::size_t i=0;i<out.size();++i) out[i]=static_cast<unsigned char>((i*31u+static_cast<std::size_t>(seq))&255u);
  return out;
}
double percentile(std::vector<double> samples,double q) {
  if(samples.empty()) return 0;
  std::sort(samples.begin(),samples.end());
  const auto idx=static_cast<std::size_t>(std::ceil(q*static_cast<double>(samples.size())))-1;
  return samples[std::min(idx,samples.size()-1)];
}
int client(int argc,char** argv) {
  const auto ip=option(argc,argv,"--host","127.0.0.1");
  const auto port=number(option(argc,argv,"--port","0"),1,65535);
  const auto count=number(option(argc,argv,"--iters","100"),1,1000000);
  const auto bytes=number(option(argc,argv,"--bytes","4096"),0,static_cast<std::uint32_t>(MAX_BODY));
  const auto window=number(option(argc,argv,"--window","1"),1,1024);
  const auto warmup=number(option(argc,argv,"--warmup","10"),0,100000);
  if(window>1 && warmup>0) { /* warmup uses the same batched protocol */ }
  auto sock=connect_to(ip,static_cast<std::uint16_t>(port));
  Frame response{};
  std::vector<double> latencies;
  const std::uint64_t total=static_cast<std::uint64_t>(warmup)+count;
  std::uint64_t recorded_bytes=0;
  Clock::time_point start{};
  for(std::uint64_t offset=0;offset<total;){
    const auto batch=static_cast<std::uint32_t>(std::min<std::uint64_t>(window,total-offset));
    std::vector<Clock::time_point> sent(batch);
    for(std::uint32_t j=0;j<batch;++j) {
      const auto seq=static_cast<std::uint32_t>(offset+j+1);
      auto bytes_data=payload_data(bytes,seq); auto wire=encode(seq,bytes_data);
      if(offset+j==warmup) start=Clock::now();
      sent[j]=Clock::now();
      write_all(sock.fd,wire.data(),wire.size());
    }
    for(std::uint32_t j=0;j<batch;++j) {
      if(!read_frame(sock.fd,response)) fail("connection closed mid-run");
      const auto expected=static_cast<std::uint32_t>(offset+j+1);
      if(response.seq!=expected || response.payload!=payload_data(bytes,expected)) fail("payload/sequence corruption or reordering");
      if(offset+j>=warmup) {
        if(window==1) latencies.push_back(std::chrono::duration<double,std::micro>(Clock::now()-sent[j]).count());
        recorded_bytes+=2u*static_cast<std::uint64_t>(bytes);
      }
    }
    offset+=batch;
  }
  const double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
  const double mbps=static_cast<double>(recorded_bytes)*8.0/(elapsed*1e6);
  const double ops=static_cast<double>(count)/elapsed;
  std::cout << std::fixed << std::setprecision(3)
    << "{\"transport\":\"tcp\",\"payload_bytes\":"<<bytes<<",\"iters\":"<<count
    << ",\"warmup\":"<<warmup<<",\"window\":"<<window
    << ",\"seconds\":"<<elapsed<<",\"messages_per_second\":"<<ops
    << ",\"bidirectional_payload_mbps\":"<<mbps;
  if(window==1) {
    std::cout << ",\"rtt_p50_us\":"<<percentile(latencies,0.50)
      << ",\"rtt_p95_us\":"<<percentile(latencies,0.95)
      << ",\"rtt_p99_us\":"<<percentile(latencies,0.99);
  }
  std::cout << ",\"verified\":true}"<<std::endl;
  return 0;
}
}
int main(int argc,char** argv) {
  try {
    if(argc<2) fail("usage: fabric_tcp server|bench [--port N] [--iters N] [--bytes N] [--window N]");
    if(std::string(argv[1])=="server") return server(argc,argv);
    if(std::string(argv[1])=="bench") return client(argc,argv);
    fail("unknown command");
  }catch(const std::exception& e){ std::cerr << "ERROR " << e.what() << '\n'; return 2; }
}
