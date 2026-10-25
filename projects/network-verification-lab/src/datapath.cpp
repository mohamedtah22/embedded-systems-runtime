// Educational UDP datapath with deterministic, black-box verification hooks.
// Not NVIDIA DOCA code, and not a hardware DPU emulator.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t MAGIC = 0x4e564552; // NVER
constexpr std::uint8_t VERSION = 1;
constexpr std::size_t HEADER_BYTES = 24;
constexpr std::size_t MAX_PAYLOAD = 1400;
constexpr std::size_t MAX_SESSIONS = 64;
constexpr std::size_t MAX_DATAGRAM = 65536;
enum class Kind : std::uint8_t {
  Data = 1, Ack = 2, StatsRequest = 3, StatsReply = 4,
  ResetRequest = 5, ResetReply = 6,
};

std::uint16_t u16(const std::uint8_t* p) {
  return (static_cast<std::uint16_t>(p[0]) << 8) | p[1];
}
std::uint32_t u32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
void put16(std::vector<std::uint8_t>& out, std::uint16_t n) {
  out.push_back(static_cast<std::uint8_t>(n >> 8));
  out.push_back(static_cast<std::uint8_t>(n));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t n) {
  out.push_back(static_cast<std::uint8_t>(n >> 24));
  out.push_back(static_cast<std::uint8_t>(n >> 16));
  out.push_back(static_cast<std::uint8_t>(n >> 8));
  out.push_back(static_cast<std::uint8_t>(n));
}
std::uint32_t crc32(const std::uint8_t* input, std::size_t length, std::uint32_t value=0xffffffffU) {
  for (std::size_t i=0; i<length; ++i) {
    value ^= input[i];
    for (int j=0; j<8; ++j) {
      value = (value >> 1) ^ ((value & 1U) ? 0xedb88320U : 0U);
    }
  }
  return value;
}

struct Header {
  std::uint8_t kind;
  std::uint16_t flags;
  std::uint32_t session;
  std::uint32_t sequence;
  std::uint16_t length;
  std::uint16_t reserved;
};

std::vector<std::uint8_t> encode(Kind kind, std::uint32_t session,
                                 std::uint32_t sequence, const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> out;
  out.reserve(HEADER_BYTES + payload.size());
  put32(out, MAGIC);
  out.push_back(VERSION);
  out.push_back(static_cast<std::uint8_t>(kind));
  put16(out, 0);              // flags
  put32(out, session);
  put32(out, sequence);
  put16(out, static_cast<std::uint16_t>(payload.size()));
  put16(out, 0);              // reserved
  auto check = crc32(out.data(), out.size());
  if (!payload.empty()) check = crc32(payload.data(), payload.size(), check);
  put32(out, check ^ 0xffffffffU);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

struct Counters {
  std::uint64_t datagrams = 0;
  std::uint64_t accepted = 0;
  std::uint64_t invalid = 0;
  std::uint64_t crc_errors = 0;
  std::uint64_t length_errors = 0;
  std::uint64_t replay_errors = 0;
  std::uint64_t header_errors = 0;
  std::uint64_t type_errors = 0;
  std::uint64_t session_errors = 0;
  std::uint64_t bytes_accepted = 0;
};

class Datapath {
 public:
  std::vector<std::uint8_t> process(const std::uint8_t* bytes, std::size_t size) {
    ++counts_.datagrams;
    if (size < HEADER_BYTES) {
      ++counts_.invalid;
      ++counts_.length_errors;
      return {};
    }
    Header h{bytes[5], u16(bytes+6), u32(bytes+8), u32(bytes+12),
             u16(bytes+16), u16(bytes+18)};
    if (u32(bytes) != MAGIC || bytes[4] != VERSION || h.flags != 0 || h.reserved != 0) {
      ++counts_.invalid;
      ++counts_.header_errors;
      return {};
    }
    if (h.length > MAX_PAYLOAD || size != HEADER_BYTES + h.length) {
      ++counts_.invalid;
      ++counts_.length_errors;
      return {};
    }
    auto sum = crc32(bytes, 20);
    if (h.length != 0) sum = crc32(bytes+HEADER_BYTES, h.length, sum);
    if ((sum ^ 0xffffffffU) != u32(bytes+20)) {
      ++counts_.invalid;
      ++counts_.crc_errors;
      return {};
    }
    if (h.kind == static_cast<std::uint8_t>(Kind::Data)) {
      const auto it = last_seq_.find(h.session);
      if (it == last_seq_.end() && last_seq_.size() == MAX_SESSIONS) {
        ++counts_.invalid;
        ++counts_.session_errors;
        return {};
      }
      // Serial-number order accepts wraparound; jumps >= half the space
      // are intentionally ambiguous and rejected.
      const auto delta = it == last_seq_.end() ? 1U : h.sequence - it->second;
      if (delta == 0 || delta >= 0x80000000U) {
        ++counts_.invalid;
        ++counts_.replay_errors;
        return {};
      }
      last_seq_[h.session] = h.sequence;
      ++counts_.accepted;
      counts_.bytes_accepted += h.length;
      std::vector<std::uint8_t> payload(bytes+HEADER_BYTES, bytes+size);
      return encode(Kind::Ack, h.session, h.sequence, payload);
    }
    if (h.kind == static_cast<std::uint8_t>(Kind::StatsRequest) && h.session==0 && h.sequence==0 && h.length==0) {
      const auto json = stats();
      return encode(Kind::StatsReply, 0, 0, {json.begin(), json.end()});
    }
    if (h.kind == static_cast<std::uint8_t>(Kind::ResetRequest) && h.session==0 && h.sequence==0 && h.length==0) {
      counts_ = {};
      last_seq_.clear();
      return encode(Kind::ResetReply, 0, 0, {});
    }
    ++counts_.invalid;
    ++counts_.type_errors;
    return {};
  }

 private:
  std::string stats() const {
    std::ostringstream out;
    out << "{\"datagrams\":" << counts_.datagrams
        << ",\"accepted\":" << counts_.accepted
        << ",\"invalid\":" << counts_.invalid
        << ",\"crc_errors\":" << counts_.crc_errors
        << ",\"length_errors\":" << counts_.length_errors
        << ",\"replay_errors\":" << counts_.replay_errors
        << ",\"header_errors\":" << counts_.header_errors
        << ",\"type_errors\":" << counts_.type_errors
        << ",\"session_errors\":" << counts_.session_errors
        << ",\"bytes_accepted\":" << counts_.bytes_accepted
        << ",\"sessions\":" << last_seq_.size() << "}";
    return out.str();
  }
  Counters counts_;
  std::map<std::uint32_t, std::uint32_t> last_seq_;
};

int run(const std::string& host, int port) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) throw std::runtime_error("socket() failed");
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(static_cast<std::uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &local.sin_addr) != 1) {
    close(fd);
    throw std::invalid_argument("invalid IPv4 address");
  }
  if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    close(fd);
    throw std::runtime_error(std::string("bind() failed: ")+strerror(errno));
  }
  socklen_t addrlen = sizeof(local);
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &addrlen) != 0) {
    close(fd);
    throw std::runtime_error("getsockname() failed");
  }
  std::cout << "READY udp=" << host << ':' << ntohs(local.sin_port) << std::endl;
  Datapath dp;
  std::array<std::uint8_t, MAX_DATAGRAM> buffer{};
  while (true) {
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    const ssize_t n = recvfrom(fd, buffer.data(), buffer.size(), 0,
                               reinterpret_cast<sockaddr*>(&peer), &len);
    if (n < 0) {
      if (errno == EINTR) continue;
      std::cerr << "recvfrom: " << strerror(errno) << '\n';
      close(fd);
      return 1;
    }
    const auto reply = dp.process(buffer.data(), static_cast<std::size_t>(n));
    if (!reply.empty()) {
      const auto written = sendto(fd, reply.data(), reply.size(), 0,
                                  reinterpret_cast<sockaddr*>(&peer), len);
      if (written != static_cast<ssize_t>(reply.size())) {
        std::cerr << "sendto: " << strerror(errno) << '\n';
      }
    }
  }
}
} // namespace

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  int port = 0;
  for (int i=1; i<argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--bind" && i+1<argc) {
      host = argv[++i];
    } else if (arg == "--port" && i+1<argc) {
      try {
        const std::string v = argv[++i];
        if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos)
          throw std::invalid_argument("port");
        const auto parsed = std::stoi(v);
        if (parsed < 0 || parsed > 65535) throw std::out_of_range("port");
        port = parsed;
      } catch (const std::exception&) {
        std::cerr << "invalid port\n";
        return 2;
      }
    } else if (arg == "--help") {
      std::cout << "Usage: nver-datapath [--bind 127.0.0.1] [--port 0]\n";
      return 0;
    } else {
      std::cerr << "unknown or incomplete option: " << arg << '\n';
      return 2;
    }
  }
  try { return run(host, port); }
  catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

