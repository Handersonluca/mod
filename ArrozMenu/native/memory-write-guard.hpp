#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace memory_guard {
using Quaternion=std::array<float,4>;
using Clock=std::chrono::steady_clock;
struct WriteFault:std::runtime_error {using std::runtime_error::runtime_error;};
inline double norm(const Quaternion& q){double n=0;for(float v:q){if(!std::isfinite(v))throw std::runtime_error("Rotacao nao finita; nada escrito");n+=static_cast<double>(v)*v;}if(std::fabs(n-1.0)>0.05)throw std::runtime_error("Rotacao fora dos limites; nada escrito");return n;}
inline bool equivalent(const Quaternion& a,const Quaternion& b){double an=norm(a),bn=norm(b),dot=0;for(unsigned i=0;i<4;i++)dot+=static_cast<double>(a[i])*b[i];return std::fabs(dot/std::sqrt(an*bn))>=1.0-1e-10;}
// The separate read / compare / write / readback operations are NOT an atomic CAS.
template<class Read,class Write,class Allowed>
bool transaction(const Quaternion& expected,const Quaternion& replacement,Read read,Write write,Allowed allowed){
    norm(expected);norm(replacement);Quaternion before{};read(before);
    if(std::memcmp(before.data(),expected.data(),16)!=0)throw std::runtime_error("Rotacao mudou antes da escrita; nada escrito");
    if(equivalent(before,replacement))return false;
    if(!allowed())throw std::runtime_error("Controle desativado; nada escrito");
    read(before);
    if(std::memcmp(before.data(),expected.data(),16)!=0)throw std::runtime_error("Rotacao mudou durante as validacoes; nada escrito");
    auto count=write(replacement);
    if(count!=16)throw WriteFault("Escrita incompleta; escrita pausada");
    Quaternion after{};
    try{read(after);}catch(const std::exception&){throw WriteFault("Confirmacao falhou; escrita pausada");}
    if(std::memcmp(after.data(),replacement.data(),16)!=0)throw WriteFault("Rotacao mudou na confirmacao; escrita pausada");
    return true;
}
inline bool fresh(Clock::time_point sampled,Clock::time_point now){return sampled!=Clock::time_point{}&&now>=sampled&&now-sampled<=std::chrono::milliseconds(100);}
inline bool singlePage(std::uint64_t address,std::uint64_t size,std::uint64_t page){return size>0&&page>=size&&address<=UINT64_MAX-(size-1)&&address/page==(address+size-1)/page;}
class Budget {
    bool faulted_=false,wrote_=false;std::uint64_t disarm_=0;Clock::time_point last_{};
public:
    void fault(std::uint64_t explicitDisarms){faulted_=true;disarm_=explicitDisarms;}
    void observeDisarm(std::uint64_t explicitDisarms){if(faulted_&&explicitDisarms!=disarm_){faulted_=false;wrote_=false;}}
    bool faulted() const{return faulted_;}
    bool ready(Clock::time_point now)const{return !faulted_&&(!wrote_||(now>=last_&&now-last_>=std::chrono::microseconds(16667)));}
    void written(Clock::time_point now){wrote_=true;last_=now;}
};
inline std::uint64_t startTicks(const std::string& stat,int expectedPid){
    auto open=stat.find('('),close=stat.rfind(')');if(open==std::string::npos||close==std::string::npos||close<=open)throw std::runtime_error("Identidade do processo invalida");
    std::istringstream prefix(stat.substr(0,open));int pid=0;if(!(prefix>>pid)||pid!=expectedPid)throw std::runtime_error("PID mudou");
    prefix>>std::ws;if(!prefix.eof())throw std::runtime_error("PID invalido");
    std::istringstream fields(stat.substr(close+1));std::string value;for(unsigned field=3;field<=22;field++)if(!(fields>>value))throw std::runtime_error("Identidade do processo incompleta");
    if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Inicio do processo invalido");
    auto result=std::stoull(value);if(!result)throw std::runtime_error("Inicio do processo invalido");return result;
}
}
