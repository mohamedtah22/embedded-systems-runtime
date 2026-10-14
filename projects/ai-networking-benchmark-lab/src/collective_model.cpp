#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
double arg(int argc,char** argv,const std::string& key,double def) {
  for(int i=1;i+1<argc;++i) if(key==argv[i]) {
    const std::string str=argv[i+1];std::size_t pos=0;
    const double x=std::stod(str,&pos);
    if(pos!=str.size() || !std::isfinite(x)) throw std::runtime_error("invalid value for "+key);
    return x;
  }
  return def;
}
}
int main(int argc,char** argv) {
  try {
    const double ranks=arg(argc,argv,"--ranks",8);
    const double tensor_mib=arg(argc,argv,"--tensor-mib",128);
    const double gbps=arg(argc,argv,"--gbps",100);
    const double latency_us=arg(argc,argv,"--latency-us",5);
    if(ranks<2 || ranks>4096 || ranks!=std::floor(ranks) || tensor_mib<0 || tensor_mib>1048576 || gbps<=0 || latency_us<0)
      throw std::runtime_error("ranks must be integer [2,4096]; tensor>=0, link gbps>0, latency>=0");
    const double bytes=tensor_mib*1024.0*1024.0;
    const double bw=gbps*1e9/8.0;
    const double alpha=latency_us*1e-6;
    // Ideal ring reduce-scatter+all-gather: 2*(p-1) steps, each sending S/p bytes.
    const double ring=2.0*(ranks-1.0)*(alpha+bytes/(ranks*bw));
    // Simplified binomial tree reduction+broadcast (full tensor per edge per stage).
    const double stages=std::ceil(std::log2(ranks));
    const double tree=2.0*stages*(alpha+bytes/bw);
    std::cout<<std::fixed<<std::setprecision(4)
      <<"{\"model\":\"idealized_alpha_beta\",\"ranks\":"<<static_cast<unsigned int>(ranks)
      <<",\"tensor_mib\":"<<tensor_mib<<",\"gbps\":"<<gbps<<",\"latency_us\":"<<latency_us
      <<",\"ring_allreduce_ms\":"<<ring*1000.0<<",\"tree_allreduce_ms\":"<<tree*1000.0
      <<",\"ring_wire_bytes_per_rank\":"<<2.0*(ranks-1.0)*bytes/ranks
      <<",\"tree_stages\":"<<static_cast<unsigned int>(stages)
      <<",\"limitations\":\"no congestion, oversubscription, PCIe, GPU, or NIC model\"}"<<'\n';
    return 0;
  }catch(const std::exception& e){ std::cerr<<"ERROR "<<e.what()<<'\n'; return 2; }
}
