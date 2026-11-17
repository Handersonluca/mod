#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <dirent.h>
#include <elf.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static std::runtime_error osError(const char* what) {
    return std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}
class ProcessMemory {
    int fd_;
    bool writable_;
    static off_t checkedAddress(std::uint64_t address) {
        static_assert(sizeof(off_t) >= 8, "Use 64-bit file offsets");
        if (address % 4 != 0 || address > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - 3)
            throw std::runtime_error("Address must be aligned to four bytes and fit in off_t");
        return static_cast<off_t>(address);
    }
public:
    explicit ProcessMemory(pid_t pid, bool writable) : fd_(-1), writable_(writable) {
        if (pid <= 0) throw std::runtime_error("PID must be positive");
        std::string path = "/proc/" + std::to_string(pid) + "/mem";
        fd_ = open(path.c_str(), (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
        if (fd_ < 0) throw osError("open process memory");
    }
    ~ProcessMemory() { if (fd_ >= 0) close(fd_); }
    ProcessMemory(const ProcessMemory&) = delete;
    ProcessMemory& operator=(const ProcessMemory&) = delete;
    void readBytes(std::uint64_t address, void* destination, std::size_t size) const {
        if (!size || size > 65536 || address > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - size)
            throw std::runtime_error("Invalid bounded memory read");
        ssize_t count;
        do { count = pread(fd_, destination, size, static_cast<off_t>(address)); } while (count < 0 && errno == EINTR);
        if (count < 0) throw osError("read process memory");
        if (static_cast<std::size_t>(count) != size) throw std::runtime_error("Incomplete bounded memory read");
    }
    std::uint32_t read32(std::uint64_t address) const {
        off_t offset = checkedAddress(address);
        std::uint32_t value = 0;
        ssize_t count;
        do { count = pread(fd_, &value, sizeof(value), offset); } while (count < 0 && errno == EINTR);
        if (count < 0) throw osError("read process memory");
        if (count != sizeof(value)) throw std::runtime_error("Incomplete memory read; process may have exited");
        return value;
    }

    void writeQuaternion(std::uint64_t address,const std::array<float,4>& expected,const std::array<float,4>& replacement) const {
        requireWritable(address);
        std::array<float,4> before;readBytes(address,before.data(),16);
        if(std::memcmp(before.data(),expected.data(),16)!=0)throw std::runtime_error("Rotacao mudou antes da escrita");
        ssize_t count;do{count=pwrite(fd_,replacement.data(),16,checkedAddress(address));}while(count<0 && errno==EINTR);
        if(count!=16)throw std::runtime_error("Escrita de rotacao incompleta");
        std::array<float,4> after;readBytes(address,after.data(),16);
        if(std::memcmp(after.data(),replacement.data(),16)!=0)throw std::runtime_error("Jogo atualizou a rotacao durante a verificacao");
    }
    void requireWritable(std::uint64_t address) const {
        if(!writable_)throw std::runtime_error("Handle somente de leitura");
        checkedAddress(address);
        if(address>static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())-16)throw std::runtime_error("Overflow de rotacao");
    }

    // Comparison, write and verification are separate operations, not an atomic CAS.
    void write32(std::uint64_t address, std::uint32_t expected, std::uint32_t replacement) const {
        off_t offset = checkedAddress(address);
        if (!writable_) throw std::runtime_error("Memory handle is read-only");
        if (read32(address) != expected) throw std::runtime_error("Current value differs from expected; nothing written");
        ssize_t count;
        do { count = pwrite(fd_, &replacement, sizeof(replacement), offset); } while (count < 0 && errno == EINTR);
        if (count < 0) throw osError("write process memory");
        if (count != sizeof(replacement)) throw std::runtime_error("Incomplete write; inspect the target value");
        if (read32(address) != replacement) throw std::runtime_error("Write readback differs; target may have changed it concurrently");
    }
};
static std::uint64_t number(const char* text) {
    std::string s(text);
    if (s.empty() || s[0] == '-' || s[0] == '+' || s.find_first_of(" \t\r\n") != std::string::npos)
        throw std::runtime_error("Invalid unsigned number");
    std::size_t consumed = 0;
    int base = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10;
    auto value = std::stoull(s, &consumed, base);
    if (consumed != s.size()) throw std::runtime_error("Invalid numeric suffix");
    return value;
}
static std::uint32_t word(const char* text) {
    auto value = number(text);
    if (value > UINT32_MAX) throw std::runtime_error("Value exceeds 32 bits");
    return static_cast<std::uint32_t>(value);
}
static void require(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
// Parent accesses a real, separate child process; the child checks the new value.
static int selfTest() {
    int addressPipe[2], releasePipe[2];
    if (pipe(addressPipe) < 0) throw osError("pipe");
    if (pipe(releasePipe) < 0) { close(addressPipe[0]); close(addressPipe[1]); throw osError("pipe"); }
    pid_t child = fork();
    if (child < 0) {
        close(addressPipe[0]); close(addressPipe[1]); close(releasePipe[0]); close(releasePipe[1]);
        throw osError("fork");
    }
    if (child == 0) {
        close(addressPipe[0]); close(releasePipe[1]);
        alignas(4) volatile std::uint32_t value = 1234;
        std::uint64_t address = reinterpret_cast<std::uintptr_t>(&value);
        // /proc can be mounted from a different PID namespace than getpid().
        char procSelf[64] = {};
        ssize_t pidLength = readlink("/proc/self", procSelf, sizeof(procSelf)-1);
        if (pidLength <= 0) _exit(12);
        pid_t procPid = static_cast<pid_t>(std::stol(std::string(procSelf, static_cast<std::size_t>(pidLength))));
        if (write(addressPipe[1], &address, sizeof(address)) != sizeof(address)) _exit(10);
        if (write(addressPipe[1], &procPid, sizeof(procPid)) != sizeof(procPid)) _exit(10);
        close(addressPipe[1]);
        char signal;
        ssize_t got;
        do { got = read(releasePipe[0], &signal, 1); } while (got < 0 && errno == EINTR);
        close(releasePipe[0]);
        _exit(got == 1 && value == 5678 ? 0 : 11);
    }
    close(addressPipe[1]); close(releasePipe[0]);
    try {
        std::uint64_t address;
        require(read(addressPipe[0], &address, sizeof(address)) == sizeof(address), "Address transfer failed");
        pid_t procPid;
        require(read(addressPipe[0], &procPid, sizeof(procPid)) == sizeof(procPid), "PID transfer failed");
        close(addressPipe[0]); addressPipe[0] = -1;
        ProcessMemory memory(procPid, true);
        require(memory.read32(address) == 1234, "Initial read failed");
        bool mismatchRejected = false;
        try { memory.write32(address, 9999, 0); } catch (const std::runtime_error&) { mismatchRejected = true; }
        require(mismatchRejected && memory.read32(address) == 1234, "Expected-value check failed");
        bool unalignedRejected = false;
        try { memory.read32(address + 1); } catch (const std::runtime_error&) { unalignedRejected = true; }
        require(unalignedRejected, "Alignment check failed");
        ProcessMemory readOnly(procPid, false);
        bool readOnlyRejected = false;
        try { readOnly.write32(address,1234,0); } catch (const std::runtime_error&) { readOnlyRejected = true; }
        require(readOnlyRejected && memory.read32(address) == 1234, "Read-only check failed");
        memory.write32(address, 1234, 5678);
        char signal = 1;
        require(write(releasePipe[1], &signal, 1) == 1, "Child release failed");
        close(releasePipe[1]); releasePipe[1] = -1;
        int status;
        pid_t waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "Child did not observe the replacement");
        std::cout << "PASS: cross-process read/write, expected-value mismatch, alignment and read-only checks; child confirmed replacement.\n";
        return 0;
    } catch (...) {
        if (addressPipe[0] >= 0) close(addressPipe[0]);
        if (releasePipe[1] >= 0) close(releasePipe[1]);
        int status;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        throw;
    }
}

struct Mapping {
    std::uint64_t start, end, offset;
    std::string permissions, path;
};
static std::vector<Mapping> processMaps(pid_t pid) {
    std::ifstream input("/proc/" + std::to_string(pid) + "/maps");
    if (!input) throw std::runtime_error("Sem acesso aos mapas do processo");
    std::vector<Mapping> maps;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string span, permissions, offset, device, inode, path;
        if (!(row >> span >> permissions >> offset >> device >> inode)) continue;
        auto dash = span.find('-');
        if (dash == std::string::npos) continue;
        std::getline(row, path);
        auto first = path.find_first_not_of(" \t");
        path = first == std::string::npos ? "" : path.substr(first);
        try {
            Mapping map{std::stoull(span.substr(0,dash),nullptr,16),std::stoull(span.substr(dash+1),nullptr,16),std::stoull(offset,nullptr,16),permissions,path};
            if (map.end > map.start) maps.push_back(map);
        } catch (const std::exception&) { continue; }
    }
    return maps;
}
static bool namesModule(const Mapping& map, const std::string& name) {
    auto pos = map.path.find("/"+name);
    if (pos == std::string::npos) return false;
    auto end = pos + 1 + name.size();
    return end == map.path.size() || map.path.substr(end) == " (deleted)";
}
static pid_t findFreeFire() {
    DIR* dir = opendir("/proc");
    if (!dir) throw osError("open /proc");
    pid_t result = 0;
    while (dirent* entry = readdir(dir)) {
        std::string name(entry->d_name);
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c){return c>='0' && c<='9';})) continue;
        std::ifstream cmdline("/proc/"+name+"/cmdline",std::ios::binary);
        std::string command;
        std::getline(cmdline,command,'\0');
        if (command != "com.dts.freefireth") continue;
        try { auto number = std::stoull(name); if (number <= static_cast<std::uint64_t>(std::numeric_limits<pid_t>::max())) { result=static_cast<pid_t>(number); break; } } catch (const std::exception&) {}
    }
    closedir(dir);
    return result;
}

static std::string buildId(const ProcessMemory& memory, std::uint64_t bias, const std::vector<Elf64_Phdr>& headers, bool fileView=false) {
    for(const auto& ph:headers) {
        if(ph.p_type!=PT_NOTE || ph.p_filesz<12 || ph.p_filesz>65536)continue;
        std::uint64_t relative=fileView ? ph.p_offset : ph.p_vaddr;
        require(relative<=std::numeric_limits<std::uint64_t>::max()-bias,"Note address overflow");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(ph.p_filesz));
        memory.readBytes(bias+relative,bytes.data(),bytes.size());
        std::size_t offset=0;
        while(offset+sizeof(Elf64_Nhdr)<=bytes.size()) {
            Elf64_Nhdr note;
            std::memcpy(&note,bytes.data()+offset,sizeof(note));offset+=sizeof(note);
            std::uint64_t nameSize=(static_cast<std::uint64_t>(note.n_namesz)+3)&~3ULL;
            std::uint64_t descSize=(static_cast<std::uint64_t>(note.n_descsz)+3)&~3ULL;
            if(nameSize>bytes.size()-offset || descSize>bytes.size()-offset-nameSize)break;
            if(note.n_type==NT_GNU_BUILD_ID && note.n_namesz>=3 && std::memcmp(bytes.data()+offset,"GNU",3)==0 && note.n_descsz>0 && note.n_descsz<=64) {
                std::ostringstream text;
                for(std::size_t i=0;i<note.n_descsz;++i)text<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(bytes[offset+nameSize+i]);
                return text.str();
            }
            offset+=static_cast<std::size_t>(nameSize+descSize);
        }
    }
    return "";
}
static int inspectFreeFire() {
    pid_t pid=findFreeFire();
    if(!pid)throw std::runtime_error("Free Fire comum nao encontrado. Abra o jogo ate o lobby e volte a este app.");
    std::cout<<"Processo encontrado: "<<pid<<"\n";
    auto maps=processMaps(pid);
    ProcessMemory memory(pid,false);
    std::uint64_t base=0,bias=0;
    std::vector<Elf64_Phdr> headers;
    for(const auto& name:{std::string("libil2cpp.so"),std::string("libunity.so")}) {
        std::uint64_t address=0;
        for(const auto& map:maps)if(namesModule(map,name) && map.offset==0 && map.permissions.find('r')!=std::string::npos){address=map.start;break;}
        if(!address){std::cout<<name<<": nao localizada\n";continue;}
        Elf64_Ehdr eh;
        memory.readBytes(address,&eh,sizeof(eh));
        require(std::memcmp(eh.e_ident,ELFMAG,SELFMAG)==0 && eh.e_ident[EI_CLASS]==ELFCLASS64 && eh.e_ident[EI_DATA]==ELFDATA2LSB && eh.e_machine==EM_AARCH64,"Cabecalho nao corresponde a ELF ARM64 little-endian");
        std::cout<<name<<": cabecalho ARM64 OK, inicio=0x"<<std::hex<<address<<std::dec<<"\n";
        if(name!="libil2cpp.so")continue;
        require(eh.e_phnum>0 && eh.e_phnum<=128 && eh.e_phentsize==sizeof(Elf64_Phdr) && eh.e_phoff<=1048576,"Tabela ELF fora dos limites esperados");
        base=address;
        headers.resize(eh.e_phnum);
        require(eh.e_phoff<=std::numeric_limits<std::uint64_t>::max()-base,"Header address overflow");
        memory.readBytes(base+eh.e_phoff,headers.data(),headers.size()*sizeof(Elf64_Phdr));
        bool gotBias=false;
        for(const auto& ph:headers)if(ph.p_type==PT_LOAD && ph.p_offset==0 && ph.p_vaddr<=base){bias=base-ph.p_vaddr;gotBias=true;break;}
        require(gotBias,"Base de carregamento nao identificada pelos segmentos ELF");
    }
    require(base!=0,"libil2cpp.so nao encontrada");
    std::cout<<"Base de carregamento: 0x"<<std::hex<<bias<<std::dec<<"\n";
    const std::uint64_t rva=0x6f277f8ULL;
    require(bias<=std::numeric_limits<std::uint64_t>::max()-rva-16,"Sample address overflow");
    std::uint64_t candidate=bias+rva;
    std::cout<<"Endereco da amostra: 0x"<<std::hex<<candidate<<std::dec<<"\n";
    bool executable=false,declaredExecutable=false;
    for(const auto& map:maps)if(map.start<=candidate && map.end>=candidate+16) {
        std::cout<<"Mapa da amostra: "<<map.permissions<<" 0x"<<std::hex<<map.start<<"-0x"<<map.end<<" off=0x"<<map.offset<<std::dec<<"\n";
        auto last=map.path.find_last_of('/');
        std::cout<<"Regiao: "<<(last==std::string::npos ? map.path : map.path.substr(last+1))<<"\n";
        executable=map.permissions.find('x')!=std::string::npos;
    }
    for(const auto& ph:headers)if(ph.p_type==PT_LOAD && (ph.p_flags&PF_X) && ph.p_vaddr<=rva && ph.p_memsz>=16 && rva-ph.p_vaddr<=ph.p_memsz-16)declaredExecutable=true;
    std::cout<<"ELF declara amostra executavel: "<<(declaredExecutable?"sim":"nao")<<"\n";
    const std::string reference="6f59b98d65ea7f7e62bb20043e46b81c074efe60";
    std::string actual;
    try {actual=buildId(memory,bias,headers);} catch(const std::exception& error) {std::cout<<"Leitura do ID virtual: "<<error.what()<<"\n";}
    std::cout<<"Build ID virtual: "<<(actual.empty()?"nao encontrado":actual)<<"\n";
    std::cout<<"Build ID de referencia: "<<reference<<"\n";
    if(!executable) {
        // Only treat this as a raw file view if maps prove a contiguous offset-zero file mapping.
        bool noteInFileView=false;
        for(const auto& ph:headers)if(ph.p_type==PT_NOTE && ph.p_filesz>0 && ph.p_filesz<=65536) {
            for(const auto& map:maps)if(namesModule(map,"libil2cpp.so") && map.start==base && map.offset==0 && ph.p_offset<=map.end-base && ph.p_filesz<=map.end-base-ph.p_offset)noteInFileView=true;
        }
        std::string rawId;
        if(noteInFileView)rawId=buildId(memory,base,headers,true);
        std::cout<<"Build ID da imagem de arquivo: "<<(rawId.empty()?"nao encontrado":rawId)<<"\n";
        if(rawId==reference) {
            std::uint64_t fileOffset=0;
            bool translated=false;
            for(const auto& ph:headers)if(ph.p_type==PT_LOAD && (ph.p_flags&PF_X) && ph.p_vaddr<=rva && ph.p_filesz>=16 && rva-ph.p_vaddr<=ph.p_filesz-16) {
                require(ph.p_offset<=std::numeric_limits<std::uint64_t>::max()-(rva-ph.p_vaddr),"File offset overflow");
                fileOffset=ph.p_offset+(rva-ph.p_vaddr);translated=true;break;
            }
            require(translated,"Amostra nao possui correspondencia em segmento de arquivo");
            require(fileOffset<=std::numeric_limits<std::uint64_t>::max()-base-16,"File address overflow");
            auto fileAddress=base+fileOffset;
            bool mapped=false;
            for(const auto& map:maps)if(namesModule(map,"libil2cpp.so") && map.start==base && map.offset==0 && map.end>=fileAddress+16 && map.permissions.find('r')!=std::string::npos)mapped=true;
            require(mapped,"A amostra de arquivo nao esta na mesma imagem continua");
            const std::uint32_t expectedFile[4]={0xb9443000U,0xd65f03c0U,0xb9043001U,0xd65f03c0U};
            bool match=true;
            for(unsigned i=0;i<4;++i)if(memory.read32(fileAddress+i*4)!=expectedFile[i])match=false;
            std::cout<<"Offset de arquivo da amostra: 0x"<<std::hex<<fileOffset<<std::dec<<"\n";
            require(match,"Build ID de arquivo confere, mas a amostra de arquivo difere");
            std::cout<<"ARQUIVO CONFERE: Build ID e amostra correspondem na imagem de arquivo.\n";
            std::cout<<"Endereco executavel ainda NAO localizado. Nao use essa base para chamar funcoes.\n";
            std::cout<<"Somente leitura; nenhum dado do jogo foi alterado.\n";
            return 3;
        }
        throw std::runtime_error("A regiao nao e executavel e a imagem de arquivo nao foi confirmada. Root nao e a causa demonstrada.");
    }
    if(actual.empty())throw std::runtime_error("Identificador virtual ausente. Correspondencia de execucao ainda nao confirmada.");
    if(actual!=reference)throw std::runtime_error("Build ID diferente: biblioteca executavel diverge do arquivo analisado. Nenhuma escrita realizada.");
    require(declaredExecutable,"O ELF nao declara a amostra dentro de segmento executavel");
    const std::uint32_t expected[4]={0xb9443000U,0xd65f03c0U,0xb9043001U,0xd65f03c0U};
    bool match=true;
    for(unsigned i=0;i<4;++i)if(memory.read32(candidate+i*4)!=expected[i])match=false;
    require(match,"Build ID confere, mas a amostra de codigo difere. Nenhuma escrita realizada.");
    std::cout<<"CHECK OK: Build ID e 16 bytes da amostra conferem.\n";
    std::cout<<"Conferencia parcial, sem hash completo da biblioteca.\n";
    std::cout<<"Somente leitura; nenhum dado do jogo foi alterado.\n";
    return 0;
}

struct CodeProbe { const char* name; std::uint64_t rva; std::array<unsigned char,32> bytes; };
static const std::array<CodeProbe,3> codeProbes = {{
{"Player.TeamIndex",0x6f277f8ULL,{{0x00,0x30,0x44,0xb9,0xc0,0x03,0x5f,0xd6,0x01,0x30,0x04,0xb9,0xc0,0x03,0x5f,0xd6,0x28,0x08,0x40,0xf9,0x20,0x00,0xc0,0x3d,0x08,0x28,0x02,0xf9,0x00,0x10,0x81,0x3d}}},
{"GameFacade.CurrentMatch",0x71bcd10ULL,{{0xfe,0x0f,0x1e,0xf8,0xf4,0x4f,0x01,0xa9,0xd3,0xee,0x01,0xb0,0x68,0x0a,0x5e,0x39,0x88,0x01,0x00,0x37,0x80,0xab,0x01,0xd0,0x00,0x70,0x42,0xf9,0xc3,0x9c,0xc5,0x94}}},
{"Camera.WorldToScreenPoint",0x9f8f984ULL,{{0x41,0x00,0x80,0x52,0x33,0xff,0xff,0x17,0x41,0x00,0x80,0x52,0x64,0xff,0xff,0x17,0x41,0x00,0x80,0x52,0x95,0xff,0xff,0x17,0x41,0x00,0x80,0x52,0xc6,0xff,0xff,0x17}}},
}};
static bool executableRange(const std::vector<Mapping>& maps,std::uint64_t address,std::size_t size) {
    if(address>std::numeric_limits<std::uint64_t>::max()-size)return false;
    for(const auto& map:maps)if(map.start<=address && map.end>=address+size && map.permissions.find('r')!=std::string::npos && map.permissions.find('x')!=std::string::npos)return true;
    return false;
}
static bool validatesCodeBase(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t base) {
    try {
        for(const auto& probe:codeProbes) {
            if(base>std::numeric_limits<std::uint64_t>::max()-probe.rva)return false;
            auto address=base+probe.rva;
            if(!executableRange(maps,address,probe.bytes.size()))return false;
            std::array<unsigned char,32> current;
            memory.readBytes(address,current.data(),current.size());
            if(current!=probe.bytes)return false;
        }
        return true;
    }catch(const std::exception&){return false;}
}
static int locateRuntimeCode(std::uint64_t* located=nullptr) {
    pid_t pid=findFreeFire();
    require(pid!=0,"Free Fire comum nao encontrado. Abra o jogo e volte ao diagnostico.");
    std::cout<<"Processo: "<<pid<<". Busca somente de leitura."<<std::endl;
    auto maps=processMaps(pid);
    ProcessMemory memory(pid,false);
    std::vector<Mapping> eligible;
    for(const auto& map:maps) {
        if(map.permissions.find('r')==std::string::npos || map.permissions.find('x')==std::string::npos || map.end-map.start<32)continue;
        const auto& path=map.path;
        if(path.find("webview")!=std::string::npos || path.find("WebView")!=std::string::npos || path.find("jit-cache")!=std::string::npos || path.find("jit-zygote-cache")!=std::string::npos)continue;
        if(path.compare(0,8,"/system/")==0 || path.compare(0,6,"/apex/")==0 || path.compare(0,8,"/vendor/")==0 || path.compare(0,12,"/system_ext/")==0 || path=="[vdso]" || path=="[vectors]")continue;
        eligible.push_back(map);
    }
    bool hasNamedTarget=std::any_of(eligible.begin(),eligible.end(),[](const Mapping& map){return namesModule(map,"libil2cpp.so");});
    if(hasNamedTarget) {
        eligible.erase(std::remove_if(eligible.begin(),eligible.end(),[](const Mapping& map){return !namesModule(map,"libil2cpp.so");}),eligible.end());
        std::cout<<"Modo: somente regioes executaveis da libil2cpp.so."<<std::endl;
    } else std::cout<<"Sem regioes nomeadas da libil2cpp.so; busca limitada nas demais regioes do app."<<std::endl;
    std::sort(eligible.begin(),eligible.end(),[](const Mapping& a,const Mapping& b){return a.start<b.start;});
    std::uint64_t targetBytes=0;
    for(const auto& map:eligible)targetBytes+=map.end-map.start;
    std::cout<<"Volume elegivel: "<<targetBytes/(1024*1024)<<" MiB"<<std::endl;
    std::cout<<"Regioes executaveis elegiveis: "<<eligible.size()<<std::endl;
    for(std::size_t i=0;i<std::min<std::size_t>(eligible.size(),12);++i) {
        const auto& map=eligible[i];
        std::string label=map.path;auto slash=label.find_last_of('/');if(slash!=std::string::npos)label=label.substr(slash+1);
        std::cout<<"XMAP 0x"<<std::hex<<map.start<<"-0x"<<map.end<<std::dec<<" "<<map.permissions<<" fileoff=0x"<<std::hex<<map.offset<<std::dec<<" "<<label<<std::endl;
    }
    const std::uint64_t limit=192ULL*1024*1024;
    std::uint64_t scanned=0,failed=0,nextProgress=32ULL*1024*1024;
    std::size_t singleHits=0;
    std::vector<std::uint64_t> candidates;
    std::array<unsigned char,65536> buffer;
    auto started=std::chrono::steady_clock::now();
    bool timedOut=false;
    const auto& anchor=codeProbes[0];
    for(const auto& map:eligible) {
        std::uint64_t position=map.start;
        while(position<map.end && scanned+anchor.bytes.size()<=limit) {
            if(std::chrono::steady_clock::now()-started>std::chrono::seconds(35)){timedOut=true;break;}
            auto count=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),std::min(map.end-position,limit-scanned)));
            if(count<anchor.bytes.size())break;
            scanned+=count;
            try {memory.readBytes(position,buffer.data(),count);}
            catch(const std::exception&){++failed;position+=std::min<std::uint64_t>(4096,map.end-position);continue;}
            auto cursor=buffer.begin();auto end=buffer.begin()+count;
            while(cursor!=end) {
                auto hit=std::search(cursor,end,anchor.bytes.begin(),anchor.bytes.end());
                if(hit==end)break;
                auto address=position+static_cast<std::uint64_t>(hit-buffer.begin());
                if(address%4==0 && address>=anchor.rva) {
                    ++singleHits;auto base=address-anchor.rva;
                    if(std::find(candidates.begin(),candidates.end(),base)==candidates.end() && validatesCodeBase(memory,maps,base)) {
                        candidates.push_back(base);
                        std::cout<<"CANDIDATO: 0x"<<std::hex<<base<<std::dec<<"; tres amostras conferem em regioes executaveis."<<std::endl;
                    }
                }
                cursor=hit+1;
            }
            if(scanned>=nextProgress){std::cout<<"Tentados: "<<scanned/(1024*1024)<<" MiB"<<std::endl;nextProgress+=32ULL*1024*1024;}
            if(candidates.size()>=4)break;
            position+=count-(anchor.bytes.size()-1);
        }
        if(timedOut || scanned+anchor.bytes.size()>limit || candidates.size()>=4)break;
    }
    std::cout<<"Resumo: "<<scanned/(1024*1024)<<" MiB tentados; amostras isoladas="<<singleHits<<"; leituras indisponiveis="<<failed<<std::endl;
    std::cout<<"Bases candidatas com tres amostras: "<<candidates.size()<<std::endl;
    if(timedOut)std::cout<<"Limite de 35 segundos atingido."<<std::endl;
    for(auto base:candidates)for(const auto& probe:codeProbes)std::cout<<probe.name<<" -> 0x"<<std::hex<<(base+probe.rva)<<std::dec<<std::endl;
    std::cout<<"Nenhuma funcao foi chamada e nenhuma memoria foi escrita."<<std::endl;
    std::cout<<"Isto ainda nao identifica objetos dos jogadores nem coordenadas da camera."<<std::endl;
    if(candidates.size()!=1){std::cout<<"Busca inconclusiva. Copie o relatorio completo."<<std::endl;return 4;}
    if(located)*located=candidates[0];
    std::cout<<"Uma base candidata localizada. Confirmacao parcial por tres amostras."<<std::endl;
    return 0;
}

static const std::array<CodeProbe,5> objectProbes = {{
{"RootCell",0x71bcd80ULL,{{0x93,0xab,0x1,0xd0,0x73,0xe6,0x44,0xf9,0x68,0x2,0x40,0xf9,0x9,0xe1,0x40,0xb9,0x89,0x0,0x0,0x35,0xe0,0x3,0x8,0xaa,0x6,0x9d,0xc5,0x94,0x68,0x2,0x40,0xf9}}},
{"StaticFields",0x71bcda0ULL,{{0x93,0xab,0x1,0xd0,0x73,0x72,0x42,0xf9,0x8,0x5d,0x40,0xf9,0x49,0x49,0x88,0x52,0x60,0x2,0x40,0xf9,0x14,0x69,0x69,0x38,0xa,0xe0,0x40,0xb9,0x6a,0x0,0x0,0x35}}},
{"MatchField",0x7315754ULL,{{0x60,0x4a,0x40,0xf9,0xfe,0x4f,0xc1,0xa8,0xc0,0x3,0x5f,0xd6,0xdd,0x3a,0xc0,0x94,0xfe,0x5f,0xbd,0xa9,0xf6,0x57,0x1,0xa9,0xf4,0x4f,0x2,0xa9,0x14,0xe4,0x1,0xb0}}},
{"LocalPlayerField",0x7202070ULL,{{0x60,0x6e,0x40,0xf9,0xfe,0x4f,0xc1,0xa8,0xc0,0x3,0x5f,0xd6,0x96,0x88,0xc4,0x94,0xff,0x3,0x1,0xd1,0xfe,0x13,0x0,0xf9,0xf4,0x4f,0x3,0xa9,0xf4,0x3,0x0,0xaa}}},
{"PlayerCameraField",0x6f2a6acULL,{{0xe8,0xbf,0x1,0xf0,0x8,0x45,0x40,0xf9,0x74,0x4a,0x43,0xf9,0x0,0x1,0x40,0xf9,0x8,0xe0,0x40,0xb9,0x48,0x0,0x0,0x35,0xbb,0xe6,0xcf,0x94,0xe0,0x3,0x14,0xaa}}}
}};

// Read-only snapshots. No remote calls, injection, or writes in this path.
static bool readableRange(const std::vector<Mapping>& maps,std::uint64_t address,std::size_t size) {
    if(!size || address<4096 || address>UINT64_MAX-size)return false;
    for(const auto& m:maps)if(m.start<=address && address+size<=m.end && m.permissions.find('r')!=std::string::npos)return true;
    return false;
}
class ObjectReader {
    const ProcessMemory& memory;
    const std::vector<Mapping>& maps;
public:
    struct Field { std::string name; std::uint32_t offset; };
    ObjectReader(const ProcessMemory& m,const std::vector<Mapping>& v):memory(m),maps(v){}
    std::uint64_t pointer(std::uint64_t address) const {
        require(address%8==0 && readableRange(maps,address,8),"Ponteiro fora de uma regiao legivel/alinhada");
        std::uint64_t value=0;memory.readBytes(address,&value,8);return value;
    }
    std::string ascii(std::uint64_t address,bool empty=false) const {
        std::string result;
        for(std::size_t i=0;i<160;++i) {
            if(!readableRange(maps,address+i,1))return "";
            unsigned char c;memory.readBytes(address+i,&c,1);
            if(!c)return !result.empty() || empty ? result : "";
            if(c<32 || c>126)return "";
            result+=static_cast<char>(c);
        }
        return "";
    }
    std::string className(std::uint64_t klass) const {
        try {
            if(!klass || klass%8)return "";
            auto name=ascii(pointer(klass+0x10));
            if(name.empty())return "";
            auto nsPointer=pointer(klass+0x18);
            if(!readableRange(maps,nsPointer,1))return "";
            auto ns=ascii(nsPointer,true);
            unsigned char first;memory.readBytes(nsPointer,&first,1);
            if(ns.empty() && first!=0)return "";
            return ns.empty()?name:ns+"."+name;
        }catch(const std::exception&){return "";}
    }
    std::string objectName(std::uint64_t object) const {
        try {return className(pointer(object));}catch(const std::exception&){return "";}
    }
    bool isA(std::uint64_t object,const std::string& expected) const {
        try {
            auto klass=pointer(object);std::vector<std::uint64_t> visited;
            for(unsigned depth=0;klass && depth<16;++depth) {
                if(std::find(visited.begin(),visited.end(),klass)!=visited.end())return false;
                visited.push_back(klass);
                auto name=className(klass);if(name.empty())return false;
                if(name==expected)return true;
                klass=pointer(klass+0x58);
            }
        }catch(const std::exception&){}
        return false;
    }
    std::vector<Field> fields(std::uint64_t object) const {
        std::vector<Field> result;
        static thread_local std::unordered_map<std::uint64_t,std::pair<std::string,std::vector<Field>>> cache;
        auto root=pointer(object);auto identity=className(root);
        auto cached=cache.find(root);if(cached!=cache.end() && cached->second.first==identity)return cached->second.second;
        try {
            auto klass=root;std::vector<std::uint64_t> seen;
            for(unsigned depth=0;klass && depth<12;++depth) {
                if(className(klass).empty() || std::find(seen.begin(),seen.end(),klass)!=seen.end())break;
                seen.push_back(klass);auto table=pointer(klass+0x80);
                // FieldInfo ARM64: name,type,parent,offset,token; validate owner for each descriptor.
                // Player has 1689 declared fields; the head reference is entry 299.
                // Keep a hard bound and stop as soon as FieldInfo.parent no longer matches.
                for(unsigned i=0;table && i<4096;++i) {
                    auto at=table+i*32;
                    if(!readableRange(maps,at,32) || pointer(at+16)!=klass)break;
                    auto name=ascii(pointer(at));if(name.empty())break;
                    auto offset=memory.read32(at+24);
                    auto type=pointer(at+8);
                    if(!readableRange(maps,type,12))break;
                    // IL2CPP TypeAttributes.Static (0x10) is in the low attributes word.
                    if((memory.read32(type+8)&0x10)==0 && offset>=16 && offset<=16384)
                        result.push_back(Field{name,offset});
                }
                klass=pointer(klass+0x58);
            }
        }catch(const std::exception&){}
        if(!result.empty() && !identity.empty()){if(cache.size()>512)cache.clear();cache[root]={identity,result};}
        return result;
    }
    bool namedPointer(std::uint64_t object,const std::string& field,std::uint64_t& value) const {
        for(const auto& f:fields(object))if(f.name==field && f.offset%8==0) {
            value=pointer(object+f.offset);return true;
        }
        return false;
    }
};
static const std::array<CodeProbe,10> unityPositionProbes = {{
{"Transform.position",0x45ea4cULL,{{0xff,0x83,0x1,0xd1,0xfe,0x57,0x4,0xa9,0xf4,0x4f,0x5,0xa9,0x15,0x7d,0x0,0xb0,0xb5,0x62,0x43,0xf9,0xf3,0x3,0x1,0xaa,0xf4,0x3,0x0,0xaa,0xe1,0x63,0x0,0x91}}},
{"Object.CachedPtr",0x71f7d0ULL,{{0x40,0x0,0x0,0xb4,0x0,0x8,0x40,0xf9,0xc0,0x3,0x5f,0xd6,0x1,0x8,0x0,0xf9,0xc0,0x3,0x5f,0xd6,0xff,0x3,0x1,0xd1,0xfe,0xb,0x0,0xf9,0xf6,0x57,0x2,0xa9}}},
{"Transform.Access",0x69304cULL,{{0xfe,0xf,0x1e,0xf8,0xf4,0x4f,0x1,0xa9,0x14,0x1c,0x40,0xf9,0xf3,0x3,0x0,0xaa,0x88,0x2,0x40,0xf9,0xe8,0x0,0x0,0xb4,0xe0,0x3,0x14,0xaa,0xe1,0x3,0x1f,0x2a}}},
{"Transform.Index",0x693078ULL,{{0x74,0x1e,0x40,0xf9,0x61,0x22,0x40,0xf9,0xe0,0x3,0x14,0xaa,0xf4,0x4f,0x41,0xa9,0xfe,0x7,0x42,0xf8,0xc0,0x3,0x5f,0xd6,0xfe,0x4f,0xbf,0xa9,0xf3,0x3,0x8,0xaa}}},
{"Transform.PositionData",0x691584ULL,{{0xfe,0x4f,0xbf,0xa9,0xf3,0x3,0x8,0xaa,0xb0,0x6,0x0,0x94,0x8,0xa4,0x41,0xa9,0x2b,0x7c,0x40,0x92,0xa,0x6,0x80,0x52,0x6c,0x7d,0xa,0x9b,0x2b,0x59,0x61,0xb8}}},
{"Transform.ParentComposition",0x6915c8ULL,{{0x6c,0x21,0xaa,0x9b,0x85,0x91,0x40,0xad,0x86,0x1,0xc0,0x3d,0x2b,0x59,0x6b,0xb8,0x0,0xdc,0x24,0x6e,0xa4,0x8,0xa0,0x4e,0x30,0x90,0xa5,0x4f,0x33,0x90,0x85,0x4f}}},
{"Camera.ViewDirty",0xb797f4ULL,{{0x8,0x58,0x54,0x39,0xf3,0x3,0x0,0xaa,0x48,0x2,0x0,0x34,0x74,0x2,0x2,0x91,0x0,0xf6,0x3,0xf,0x8,0xf0,0xb7,0x52,0xe1,0x3,0x0,0x91,0xe0,0x3,0x14,0xaa}}},
{"Camera.ViewCache",0xb79844ULL,{{0x60,0x2,0x2,0x91,0xf4,0x4f,0x45,0xa9,0xfe,0x23,0x40,0xf9,0xff,0x83,0x1,0x91,0xc0,0x3,0x5f,0xd6,0xfe,0xf,0x1e,0xf8,0xf4,0x4f,0x1,0xa9,0x8,0x50,0x54,0x39}}},
{"Camera.ProjectionDirty",0xb79860ULL,{{0x8,0x50,0x54,0x39,0xf3,0x3,0x0,0xaa,0xe8,0x4,0x0,0x34,0x68,0x1a,0x45,0xb9,0x1f,0x9,0x0,0x71,0x0,0x2,0x0,0x54,0x1f,0x5,0x0,0x71,0x21,0x4,0x0,0x54}}},
{"Camera.ProjectionCache",0xb79900ULL,{{0x7f,0x52,0x14,0x39,0x60,0x2,0x3,0x91,0xf4,0x4f,0x41,0xa9,0xfe,0x7,0x42,0xf8,0xc0,0x3,0x5f,0xd6,0xff,0x83,0x1,0xd1,0xfe,0x57,0x4,0xa9,0xf4,0x4f,0x5,0xa9}}}
}};
static const std::array<CodeProbe,3> positionManagedProbes = {{
{"CameraController.MainCamera",0x71843a0ULL,{{0x60,0x12,0x40,0xf9,0xfe,0x4f,0xc1,0xa8,0xc0,0x3,0x5f,0xd6,0xca,0x7f,0xc6,0x94,0xe8,0xf,0x1e,0xfc,0xfe,0x7,0x0,0xf9,0xf4,0x4f,0x1,0xa9,0x94,0xf0,0x1,0xb0}}},
{"Entity.CachedTransform",0x8251be8ULL,{{0xf4,0x3,0x13,0xaa,0x95,0x8e,0x45,0xf8,0xc0,0x2,0x40,0xf9,0x8,0xe0,0x40,0xb9,0x48,0x0,0x0,0x35,0x6d,0x49,0x83,0x94,0xe0,0x3,0x15,0xaa,0xe1,0x3,0x1f,0xaa}}},
{"Transform.PositionCache",0x9fe5a74ULL,{{0x14,0x7e,0x0,0xf0,0x88,0xf2,0x46,0xf9,0xf3,0x3,0x0,0xaa,0xff,0xb,0x0,0xb9,0xff,0x3,0x0,0xf9,0xc8,0x0,0x0,0xb5,0x0,0xce,0xfb,0xd0,0x0,0xac,0x9,0x91}}}
}};

struct Vec3 {double x,y,z;};
struct Matrix4 {std::array<float,16> v;};
static bool finitePoint(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<1e7&&std::fabs(p.y)<1e7&&std::fabs(p.z)<1e7;}
static Vec3 rotatePoint(const float* q,Vec3 v) {
    Vec3 u{q[0],q[1],q[2]};double w=q[3],dot=u.x*v.x+u.y*v.y+u.z*v.z,n=u.x*u.x+u.y*u.y+u.z*u.z;
    return {2*dot*u.x+(w*w-n)*v.x+2*w*(u.y*v.z-u.z*v.y),
            2*dot*u.y+(w*w-n)*v.y+2*w*(u.z*v.x-u.x*v.z),
            2*dot*u.z+(w*w-n)*v.z+2*w*(u.x*v.y-u.y*v.x)};
}
static bool matchesProbe(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t base,const CodeProbe& p){
    try {if(base>UINT64_MAX-p.rva || !executableRange(maps,base+p.rva,p.bytes.size()))return false;
        std::array<unsigned char,32> bytes;memory.readBytes(base+p.rva,bytes.data(),bytes.size());return bytes==p.bytes;
    }catch(const std::exception&){return false;}
}
static std::uint64_t locateUnityPositionCode(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t ilBase) {
    for(const auto& p:positionManagedProbes)require(matchesProbe(memory,maps,ilBase,p),"Trecho IL2CPP de posicao diverge do arquivo");
    ObjectReader read(memory,maps);std::vector<std::uint64_t> possible,valid;
    try {auto cached=read.pointer(ilBase+0xafa8de0ULL);if(cached>=0x45ea4cULL)possible.push_back(cached-0x45ea4cULL);}catch(const std::exception&){}
    // Uploaded libunity ELF PT_LOAD executable: p_vaddr - p_offset = 0x4000.
    for(const auto& map:maps)if(namesModule(map,"libunity.so") && map.permissions.find('x')!=std::string::npos && map.start>=map.offset+0x4000ULL)
        possible.push_back(map.start-map.offset-0x4000ULL);
    std::sort(possible.begin(),possible.end());possible.erase(std::unique(possible.begin(),possible.end()),possible.end());
    for(auto base:possible)if(std::all_of(unityPositionProbes.begin(),unityPositionProbes.end(),[&](const CodeProbe& p){return matchesProbe(memory,maps,base,p);}))valid.push_back(base);
    require(valid.size()==1,"Codigo Unity de posicao/camera nao confirmado por dez amostras");
    std::cout<<"Unity: base candidata 0x"<<std::hex<<valid[0]<<std::dec<<"; dez trechos do caminho conferem. Confirmacao parcial."<<std::endl;
    return valid[0];
}
static Vec3 readTransformPosition(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t managedTransform) {
    ObjectReader read(memory,maps);
    require(read.isA(managedTransform,"UnityEngine.Transform"),"Referencia nao tem tipo Transform");
    std::uint64_t native=0;require(read.namedPointer(managedTransform,"m_CachedPtr",native) && native!=0,"Transform sem ponteiro nativo refletido");
    require(native==read.pointer(managedTransform+0x10),"Layout do ponteiro nativo diverge da rotina conferida");
    require(readableRange(maps,native,0x48),"Transform nativo incompleto");
    auto access=read.pointer(native+0x38);auto index=memory.read32(native+0x40);
    require(readableRange(maps,access,0x28),"Acesso ao transform incompleto");
    require(index<1000000,"Indice de transform fora do limite");
    auto transforms=read.pointer(access+0x18),parents=read.pointer(access+0x20);
    require(transforms<UINT64_MAX-48000000ULL && parents<UINT64_MAX-4000000ULL,"Overflow nos arrays do transform");
    struct Node {std::uint32_t index;std::array<float,12> data;std::int32_t parent;};
    std::vector<Node> nodes;std::vector<std::uint32_t> seen;
    for(unsigned depth=0;depth<256;++depth) {
        require(std::find(seen.begin(),seen.end(),index)==seen.end(),"Ciclo na hierarquia de transform");seen.push_back(index);
        require(readableRange(maps,transforms+static_cast<std::uint64_t>(index)*48,48) && readableRange(maps,parents+static_cast<std::uint64_t>(index)*4,4),"Dados do transform fora de regiao legivel");
        Node n;n.index=index;memory.readBytes(transforms+static_cast<std::uint64_t>(index)*48,n.data.data(),48);
        auto raw=memory.read32(parents+static_cast<std::uint64_t>(index)*4);std::memcpy(&n.parent,&raw,4);nodes.push_back(n);
        for(unsigned j=0;j<11;++j)if(j!=3)require(std::isfinite(n.data[j]) && std::fabs(n.data[j])<1e7,"Transform contem componente invalido");
        if(n.parent<0)break;
        require(n.parent<1000000,"Pai de transform fora do limite");index=static_cast<std::uint32_t>(n.parent);
        require(depth<255,"Hierarquia excede limite");
    }
    require(!nodes.empty() && nodes.back().parent<0,"Hierarquia incompleta");
    Vec3 result{nodes[0].data[0],nodes[0].data[1],nodes[0].data[2]};
    for(std::size_t i=1;i<nodes.size();++i) {
        const auto& n=nodes[i];double norm=0;for(unsigned j=4;j<8;++j)norm+=static_cast<double>(n.data[j])*n.data[j];
        require(std::fabs(norm-1)<0.02,"Quaternion de pai nao normalizado");
        result={result.x*n.data[8],result.y*n.data[9],result.z*n.data[10]};result=rotatePoint(n.data.data()+4,result);
        result={result.x+n.data[0],result.y+n.data[1],result.z+n.data[2]};require(finitePoint(result),"Posicao reconstruida invalida");
    }
    // Reject races on all consumed nodes and on the owning pointers; this is not an atomic snapshot.
    for(const auto& n:nodes) {
        std::array<float,12> again;memory.readBytes(transforms+static_cast<std::uint64_t>(n.index)*48,again.data(),48);
        require(std::memcmp(again.data(),n.data.data(),48)==0,"Transform mudou durante a leitura; repita parado");
        auto parent=memory.read32(parents+static_cast<std::uint64_t>(n.index)*4);std::uint32_t initial;std::memcpy(&initial,&n.parent,4);
        require(parent==initial,"Pai de transform mudou durante a leitura");
    }
    require(read.pointer(managedTransform+0x10)==native && read.pointer(native+0x38)==access && memory.read32(native+0x40)==nodes[0].index && read.pointer(access+0x18)==transforms && read.pointer(access+0x20)==parents,"Referencias do transform mudaram durante a leitura");
    require(finitePoint(result),"Posicao nao finita");return result;
}
static Vec3 readPlayerPosition(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t player) {
    ObjectReader read(memory,maps);std::uint64_t transform=0;
    require(read.isA(player,"COW.GamePlay.Player"),"Objeto nao e Player");
    require(read.namedPointer(player,"m_CachedTransform",transform) && transform,"Jogador sem Transform em cache");
    require(transform==read.pointer(player+0x58),"Campo CachedTransform diverge do getter conferido");
    auto position=readTransformPosition(memory,maps,transform);
    require(read.pointer(player+0x58)==transform,"Transform do jogador mudou");return position;
}
static bool validViewMatrix(const Matrix4& matrix){
    const auto& m=matrix.v;for(auto x:m)if(!std::isfinite(x)||std::fabs(x)>1e7)return false;
    if(std::fabs(m[3])>1e-4 || std::fabs(m[7])>1e-4 || std::fabs(m[11])>1e-4 || std::fabs(m[15]-1)>1e-4)return false;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
        double dot=0;for(int k=0;k<3;++k)dot+=static_cast<double>(m[i*4+k])*m[j*4+k];
        if(std::fabs(dot-(i==j?1.0:0.0))>0.02)return false;
    }
    return true;
}
static bool validPerspectiveMatrix(const Matrix4& matrix){
    const auto& m=matrix.v;for(auto x:m)if(!std::isfinite(x)||std::fabs(x)>1e7)return false;
    return std::fabs(m[0])>0.01 && std::fabs(m[5])>0.01 && std::fabs(m[3])<1e-4 && std::fabs(m[7])<1e-4
        && std::fabs(m[11]+1)<1e-4 && std::fabs(m[15])<1e-4 && std::fabs(m[14])>1e-8;
}
static bool readCameraMatrices(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t camera,Matrix4& view,Matrix4& projection) {
    ObjectReader read(memory,maps);require(read.isA(camera,"UnityEngine.Camera"),"Referencia de camera com tipo divergente");
    std::uint64_t native=0;require(read.namedPointer(camera,"m_CachedPtr",native) && native,"Camera sem ponteiro nativo refletido");
    require(read.pointer(camera+0x10)==native,"Layout de camera diverge do acesso conferido");
    require(readableRange(maps,native,0x518),"Objeto nativo de camera incompleto");
    unsigned char dirtyView=0,dirtyProjection=0;memory.readBytes(native+0x516,&dirtyView,1);memory.readBytes(native+0x514,&dirtyProjection,1);
    std::cout<<"Camera cache: view_dirty="<<static_cast<unsigned>(dirtyView)<<" projection_dirty="<<static_cast<unsigned>(dirtyProjection)<<std::endl;
    if(dirtyView || dirtyProjection){std::cout<<"Matrizes pendentes: caches marcados desatualizados; nenhuma rotina de atualizacao chamada."<<std::endl;return false;}
    memory.readBytes(native+0x80,view.v.data(),64);memory.readBytes(native+0xc0,projection.v.data(),64);
    Matrix4 v2,p2;memory.readBytes(native+0x80,v2.v.data(),64);memory.readBytes(native+0xc0,p2.v.data(),64);
    unsigned char dv2=0,dp2=0;memory.readBytes(native+0x516,&dv2,1);memory.readBytes(native+0x514,&dp2,1);
    require(!dv2 && !dp2 && read.pointer(camera+0x10)==native && std::memcmp(view.v.data(),v2.v.data(),64)==0 && std::memcmp(projection.v.data(),p2.v.data(),64)==0,"Camera mudou durante leitura");
    require(validViewMatrix(view),"Cache view nao passou na validacao de matriz afim/ortonormal");
    require(validPerspectiveMatrix(projection),"Cache projection nao passou na validacao de perspectiva");
    return true;
}
static bool projectViewport(const Matrix4& view,const Matrix4& projection,Vec3 world,double& u,double& v){
    double input[4]={world.x,world.y,world.z,1},camera[4]={},clip[4]={};
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)camera[row]+=view.v[col*4+row]*input[col];
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)clip[row]+=projection.v[col*4+row]*camera[col];
    if(!std::isfinite(clip[3]) || clip[3]<=1e-5)return false;
    u=(clip[0]/clip[3]+1)*0.5;v=(1-clip[1]/clip[3])*0.5;
    return std::isfinite(u)&&std::isfinite(v);
}
static const std::array<CodeProbe,5> liveViewProbes = {{
{"Camera.TransformType",0xb79820ULL,{{0x60,0x1a,0x40,0xf9,0x61,0x44,0x0,0xb0,0x21,0x0,0x32,0x91,0xa0,0xc5,0xe6,0x97,0xe8,0x3,0x0,0x91,0x14,0x74,0xec,0x97,0xe1,0x3,0x0,0x91,0xe0,0x3,0x14,0xaa}}},
{"GameObject.Components",0x52aeacULL,{{0xb,0x20,0x40,0xf9,0x8b,0x1,0x0,0xb4,0x9,0x18,0x40,0xf9,0x28,0x28,0x46,0x29,0x6b,0xed,0x7c,0xd3,0x29,0x21,0x0,0x91,0x2c,0x81,0x5f,0xb8,0x8c,0x1,0x8,0x4b}}},
{"GameObject.ComponentReturn",0x52aee8ULL,{{0x20,0x1,0x40,0xf9,0xc0,0x3,0x5f,0xd6,0xa,0x20,0x40,0xf9,0x6a,0x1,0x0,0xb4,0x8,0x18,0x40,0xf9,0x29,0x30,0x40,0xb9,0x4a,0xed,0x7c,0xd3,0x8,0x21,0x0,0x91}}},
{"Camera.RigidInverse",0x696884ULL,{{0xfe,0x4f,0xbf,0xa9,0xf3,0x3,0x8,0xaa,0xf0,0xf1,0xff,0x97,0xa,0xb0,0x41,0xa9,0x28,0x7c,0x40,0x92,0xd,0x6,0x80,0x52,0x6b,0xd7,0xff,0xf0,0x8,0x29,0xd,0x9b}}},
{"Camera.ZFlip",0xc0eae8ULL,{{0x20,0x0,0x40,0xbd,0x1f,0x10,0x0,0xb9,0x1f,0x20,0x0,0xb9,0x1f,0x30,0x0,0xb9,0x1f,0x4,0x0,0xb9,0x0,0x0,0x0,0xbd,0x20,0x4,0x40,0xbd,0x1f,0x24,0x0,0xb9}}}
}};

struct Pose {Vec3 position;std::array<double,4> rotation;bool positiveScale;};
static std::array<double,4> multiplyQuaternion(const std::array<double,4>& a,const std::array<double,4>& b){
    return {{a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]}};
}
class SnapshotChanged:public std::runtime_error {public:explicit SnapshotChanged(const char* value):std::runtime_error(value){}};
static bool samePoseComponents(const std::array<float,12>& a,const std::array<float,12>& b){
    for(unsigned i=0;i<11;++i)if(i!=3 && std::memcmp(&a[i],&b[i],sizeof(float))!=0)return false;
    return true;
}
static Pose readNativePoseOnce(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t native){
    ObjectReader read(memory,maps);require(readableRange(maps,native,0x48),"Transform nativo incompleto");
    auto access=read.pointer(native+0x38);auto index=memory.read32(native+0x40);
    require(readableRange(maps,access,0x28),"Acesso ao transform incompleto");
    require(index<1000000,"Indice de transform fora do limite");
    auto transforms=read.pointer(access+0x18),parents=read.pointer(access+0x20);
    require(transforms<UINT64_MAX-48000000ULL && parents<UINT64_MAX-4000000ULL,"Overflow nos arrays do transform");
    struct Node {std::uint32_t index;std::array<float,12> data;std::int32_t parent;};
    std::vector<Node> nodes;std::vector<std::uint32_t> seen;
    for(unsigned depth=0;depth<256;++depth) {
        require(std::find(seen.begin(),seen.end(),index)==seen.end(),"Ciclo na hierarquia de transform");seen.push_back(index);
        require(readableRange(maps,transforms+static_cast<std::uint64_t>(index)*48,48) && readableRange(maps,parents+static_cast<std::uint64_t>(index)*4,4),"Dados do transform fora de regiao legivel");
        Node n;n.index=index;memory.readBytes(transforms+static_cast<std::uint64_t>(index)*48,n.data.data(),48);
        auto raw=memory.read32(parents+static_cast<std::uint64_t>(index)*4);std::memcpy(&n.parent,&raw,4);nodes.push_back(n);
        for(unsigned j=0;j<11;++j)if(j!=3)require(std::isfinite(n.data[j]) && std::fabs(n.data[j])<1e7,"Transform contem componente invalido");
        if(n.parent<0)break;
        require(n.parent<1000000,"Pai de transform fora do limite");index=static_cast<std::uint32_t>(n.parent);
        require(depth<255,"Hierarquia excede limite");
    }
    require(!nodes.empty() && nodes.back().parent<0,"Hierarquia incompleta");
    Vec3 result{nodes[0].data[0],nodes[0].data[1],nodes[0].data[2]};
    std::array<double,4> rotation{{nodes[0].data[4],nodes[0].data[5],nodes[0].data[6],nodes[0].data[7]}};
    bool positive=true;
    for(const auto& n:nodes)positive=positive && n.data[8]>0 && n.data[9]>0 && n.data[10]>0;
    for(std::size_t i=1;i<nodes.size();++i) {
        const auto& n=nodes[i];double norm=0;for(unsigned j=4;j<8;++j)norm+=static_cast<double>(n.data[j])*n.data[j];
        require(std::fabs(norm-1)<0.02,"Quaternion de pai nao normalizado");
        result={result.x*n.data[8],result.y*n.data[9],result.z*n.data[10]};result=rotatePoint(n.data.data()+4,result);
        rotation=multiplyQuaternion({{n.data[4],n.data[5],n.data[6],n.data[7]}},rotation);
        result={result.x+n.data[0],result.y+n.data[1],result.z+n.data[2]};require(finitePoint(result),"Posicao reconstruida invalida");
    }
    // Reject races on all consumed nodes and on the owning pointers; this is not an atomic snapshot.
    for(const auto& n:nodes) {
        std::array<float,12> again;memory.readBytes(transforms+static_cast<std::uint64_t>(n.index)*48,again.data(),48);
        if(!samePoseComponents(again,n.data))throw SnapshotChanged("Transform atualizado durante a leitura");
        auto parent=memory.read32(parents+static_cast<std::uint64_t>(n.index)*4);std::uint32_t initial;std::memcpy(&initial,&n.parent,4);
        if(parent!=initial)throw SnapshotChanged("Pai de transform atualizado durante a leitura");
    }
    if(!(read.pointer(native+0x38)==access && memory.read32(native+0x40)==nodes[0].index && read.pointer(access+0x18)==transforms && read.pointer(access+0x20)==parents))throw SnapshotChanged("Referencias do transform atualizadas durante a leitura");
    require(finitePoint(result),"Posicao nao finita");return Pose{result,rotation,positive};
}
static Pose readNativePose(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t native){
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
    for(unsigned attempt=0;;++attempt)try{return readNativePoseOnce(memory,maps,native);}catch(const SnapshotChanged&){if(attempt>=2 || std::chrono::steady_clock::now()>=deadline)throw;}
}
static Matrix4 rigidCameraView(const Pose& pose){
    require(pose.positiveScale,"Escala negativa de camera nao suportada");
    double norm=0;for(auto q:pose.rotation)norm+=q*q;
    require(std::isfinite(norm)&&std::fabs(norm-1)<0.02,"Rotacao da camera invalida");
    float q[4];for(unsigned i=0;i<4;++i)q[i]=static_cast<float>(pose.rotation[i]/std::sqrt(norm));
    Vec3 right=rotatePoint(q,{1,0,0}),up=rotatePoint(q,{0,1,0}),forward=rotatePoint(q,{0,0,1});
    Matrix4 result{};const double axes[3][3]={{right.x,right.y,right.z},{up.x,up.y,up.z},{-forward.x,-forward.y,-forward.z}};
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)result.v[col*4+row]=static_cast<float>(axes[row][col]);
    for(unsigned row=0;row<3;++row)result.v[12+row]=static_cast<float>(-(axes[row][0]*pose.position.x+axes[row][1]*pose.position.y+axes[row][2]*pose.position.z));
    result.v[15]=1;require(validViewMatrix(result),"Visao reconstruida invalida");return result;
}
static void verifyLiveViewCode(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t unity){
    for(const auto& p:liveViewProbes)require(matchesProbe(memory,maps,unity,p),"Codigo da camera ao vivo difere da referencia");
}
static std::uint64_t cameraNativeTransform(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t unity,std::uint64_t nativeCamera){
    ObjectReader read(memory,maps);auto gameObject=read.pointer(nativeCamera+0x30);
    auto components=read.pointer(gameObject+0x30),count=read.pointer(gameObject+0x40);
    require(count>0 && count<=256 && readableRange(maps,components,static_cast<std::size_t>(count*16)),"Lista de componentes da camera invalida");
    // Exact native GetComponent range test from 0x52aeac; Transform descriptor from the view getter.
    auto first=memory.read32(unity+0x1406c80ULL+0x30),range=memory.read32(unity+0x1406c80ULL+0x34);
    require(range>0 && range<100000,"Descritor Transform nativo invalido");
    std::uint64_t transform=0;
    for(std::uint64_t i=0;i<count;++i){auto type=memory.read32(components+i*16);
        if(static_cast<std::uint32_t>(type-first)<range){auto candidate=read.pointer(components+i*16+8);if(candidate){require(!transform,"Transform da camera ambiguo");transform=candidate;}}}
    require(transform && read.pointer(transform+0x30)==gameObject,"Transform nao pertence ao GameObject da camera");
    require(read.pointer(nativeCamera+0x30)==gameObject && read.pointer(gameObject+0x30)==components && read.pointer(gameObject+0x40)==count,"Componentes mudaram durante a leitura");
    return transform;
}
static bool readLiveCameraMatricesOnce(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t unity,std::uint64_t camera,Matrix4& view,Matrix4& projection,Vec3& cameraPosition){
    ObjectReader read(memory,maps);require(read.isA(camera,"UnityEngine.Camera"),"Camera com tipo divergente");
    auto native=read.pointer(camera+0x10);require(readableRange(maps,native,0x518),"Camera nativa indisponivel");
    unsigned char dirtyView=0,dirtyProjection=0;memory.readBytes(native+0x516,&dirtyView,1);memory.readBytes(native+0x514,&dirtyProjection,1);
    if(dirtyProjection)throw SnapshotChanged("Projecao da camera marcada desatualizada");
    auto transform=cameraNativeTransform(memory,maps,unity,native);auto pose=readNativePose(memory,maps,transform);cameraPosition=pose.position;
    if(dirtyView)view=rigidCameraView(pose);
    else try{Matrix4 cachedProjection{};require(readCameraMatrices(memory,maps,camera,view,cachedProjection),"Cache de camera indisponivel");}catch(const std::runtime_error& e){if(std::string(e.what())=="Camera mudou durante leitura")throw SnapshotChanged("Camera atualizada durante leitura");throw;}
    memory.readBytes(native+0xc0,projection.v.data(),64);Matrix4 again;memory.readBytes(native+0xc0,again.v.data(),64);
    unsigned char dp=0;memory.readBytes(native+0x514,&dp,1);
    if(!(!dp && read.pointer(camera+0x10)==native && std::memcmp(again.v.data(),projection.v.data(),64)==0))throw SnapshotChanged("Projecao atualizada durante a leitura");
    require(validPerspectiveMatrix(projection),"Formato da projecao invalido");return true;
}

static bool readLiveCameraMatrices(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t unity,std::uint64_t camera,Matrix4& view,Matrix4& projection,Vec3& position){
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2);
    for(unsigned attempt=0;;++attempt)try{return readLiveCameraMatricesOnce(memory,maps,unity,camera,view,projection,position);}catch(const SnapshotChanged&){if(attempt>=2 || std::chrono::steady_clock::now()>=deadline)throw;}
}
struct RuntimeRefs {std::uint64_t game=0,match=0,local=0,camera=0;std::vector<std::uint64_t> players;};

static int inspectPositions(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t base,const std::vector<std::uint64_t>& players,std::uint64_t local,std::uint64_t camera) {
    std::cout<<"POSICOES: coleta experimental com duas amostras; sem escrita."<<std::endl;
    std::uint64_t unity=0;
    try {unity=locateUnityPositionCode(memory,maps,base);verifyLiveViewCode(memory,maps,unity);}catch(const std::exception& e){std::cout<<"POSICOES PENDENTES: "<<e.what()<<std::endl;return 6;}
    ObjectReader read(memory,maps);std::vector<std::pair<std::uint64_t,Vec3>> first;
    unsigned projectionCount=0;bool cameraOK=false,localOK=false;
    for(unsigned sample=0;sample<2;++sample) {
        if(sample){std::cout<<"Segunda amostra em 2 segundos..."<<std::endl;usleep(2000000);}
        std::cout<<"AMOSTRA "<<sample+1<<std::endl;
        Matrix4 view{},projection{};bool matrices=false;
        try {Vec3 cameraPosition{};matrices=readLiveCameraMatrices(memory,maps,unity,camera,view,projection,cameraPosition);}catch(const std::exception& e){std::cout<<"Camera: "<<e.what()<<std::endl;}
        if(matrices){cameraOK=true;std::cout<<"Matrizes: cache legivel, estavel e formato conferido. Camera ativa ainda nao provada."<<std::endl;}
        for(std::size_t i=0;i<std::min<std::size_t>(players.size(),24);++i) {
            auto object=players[i];auto name=read.objectName(object);
            if(read.isA(object,"COW.GamePlay.Player_Watching")){std::cout<<"IGNORADO Player_Watching: 0x"<<std::hex<<object<<std::dec<<std::endl;continue;}
            try {
                auto position=readPlayerPosition(memory,maps,object);
                std::cout<<"POS 0x"<<std::hex<<object<<std::dec<<" tipo="<<name<<(object==local?" LOCAL":"")<<std::fixed<<std::setprecision(3)
                    <<" xyz=("<<position.x<<", "<<position.y<<", "<<position.z<<")";
                if(object==local)localOK=true;
                if(!sample)first.push_back({object,position});
                else for(const auto& old:first)if(old.first==object){auto a=position.x-old.second.x,b=position.y-old.second.y,c=position.z-old.second.z;std::cout<<" deslocamento="<<std::sqrt(a*a+b*b+c*c);break;}
                double u=0,v=0;
                if(matrices && projectViewport(view,projection,position,u,v)) {
                    ++projectionCount;std::cout<<" viewport_top_left=("<<u<<", "<<v<<") "<<((u>=0&&u<=1&&v>=0&&v<=1)?"dentro":"fora");
                }else std::cout<<" projecao=pendente/ou_atras";
                std::cout<<std::endl;
            }catch(const std::exception& e){std::cout<<"POS PENDENTE 0x"<<std::hex<<object<<std::dec<<": "<<e.what()<<std::endl;}
        }
    }
    std::cout<<"Resumo de posicoes: primeiras validas="<<first.size()<<"; local="<<(localOK?"sim":"nao")<<"; camera_cache="<<(cameraOK?"sim":"nao")<<"; projecoes candidatas="<<projectionCount<<std::endl;
    std::cout<<"Snapshots nao atomicos. Teste de movimento e correspondencia visual ainda necessarios; sem filtro completo de jogadores ativos. Nenhuma mira foi alterada e nenhuma linha desenhada."<<std::endl;
    return localOK && !first.empty() && cameraOK && projectionCount>0 ? 0 : 6;
}

static int inspectRuntimeObjects(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t base,bool positions=false,RuntimeRefs* capture=nullptr,std::ostream* output=nullptr) {
    auto& out=output?*output:std::cout;
    out<<"OBJETOS: snapshot experimental somente de leitura."<<std::endl;
    out<<"Caminhos estaticos podem diferir de substituicoes de runtime; checagem de tipos obrigatoria."<<std::endl;
    for(const auto& probe:objectProbes) {
        require(base<=UINT64_MAX-probe.rva,"Overflow na amostra");
        require(executableRange(maps,base+probe.rva,probe.bytes.size()),"Trecho do caminho fora de regiao executavel");
        std::array<unsigned char,32> actual;memory.readBytes(base+probe.rva,actual.data(),actual.size());
        require(actual==probe.bytes,"Codigo do caminho de objetos difere do arquivo. Leitura interrompida.");
    }
    ObjectReader read(memory,maps);
    auto printObject=[&](const char* label,std::uint64_t object) {
        auto type=read.objectName(object);
        out<<label<<": 0x"<<std::hex<<object<<std::dec<<" tipo="<<(type.empty()?"nao validado":type)<<std::endl;
    };
    auto rootSlot=read.pointer(base+0xa72e4e0ULL);
    auto rootClass=read.pointer(rootSlot);
    auto rootName=read.className(rootClass);
    out<<"Classe raiz: "<<(rootName.empty()?"nao validada":rootName)<<std::endl;
    if(rootName.empty()){out<<"INCONCLUSIVO: layout da classe raiz nao validado."<<std::endl;return 5;}
    auto staticFields=read.pointer(rootClass+0xb8);
    std::vector<std::uint64_t> games;
    for(auto offset:{0ULL,8ULL}) {
        auto game=read.pointer(staticFields+offset);printObject(offset?"Raiz estatica[1]":"Raiz estatica[0]",game);
        if(game && read.isA(game,"COW.MatchGame") && std::find(games.begin(),games.end(),game)==games.end())games.push_back(game);
    }
    if(games.size()!=1){out<<"INCONCLUSIVO: esperado um MatchGame com tipo conferido; encontrados="<<games.size()<<std::endl;return 5;}
    auto game=games[0],match=read.pointer(game+0x90);
    printObject("Partida candidata",match);
    if(!read.isA(match,"COW.GamePlay.JMAGGLCNGIG")){out<<"INCONCLUSIVO: tipo da partida difere do caminho estatico."<<std::endl;return 5;}
    auto local=read.pointer(match+0xd8);
    printObject("Jogador local candidato",local);
    if(!read.isA(local,"COW.GamePlay.Player")){out<<"INCONCLUSIVO: jogador local ausente ou tipo divergente."<<std::endl;return 5;}
    require(readableRange(maps,local+0x430,4),"Campo de equipe indisponivel");
    out<<"Player.TeamIndex (valor bruto): "<<memory.read32(local+0x430)<<std::endl;
    auto camera=read.pointer(local+0x690);printObject("Referencia de camera do jogador",camera);
    bool cameraValid=read.isA(camera,"UnityEngine.Camera");
    out<<"Referencia tem tipo UnityEngine.Camera: "<<(cameraValid?"sim":"nao")<<"; atividade/matrizes ainda nao confirmadas."<<std::endl;
    std::uint64_t manager=0,managerCamera=0;
    if(read.namedPointer(game,"m_CameraControllerManager",manager)) {
        printObject("Gerenciador de camera",manager);
        if(manager && !read.objectName(manager).empty()) {
            unsigned printed=0;
            for(const auto& f:read.fields(manager)) {
                if(f.offset%8)continue;
                auto object=read.pointer(manager+f.offset);
                if(read.isA(object,"UnityEngine.Camera")) {
                    managerCamera=object;
                    out<<"Campo de camera no gerenciador: "<<f.name<<" +0x"<<std::hex<<f.offset<<std::dec<<std::endl;
                    printObject("Camera referenciada",object);
                    if(++printed==4)break;
                }
            }
        }
    }
    std::vector<std::uint64_t> players{local};
    unsigned collections=0;std::uint64_t attempted=0;
    auto addPlayer=[&](std::uint64_t object) {
        if(object && object%8==0 && std::find(players.begin(),players.end(),object)==players.end() && read.isA(object,"COW.GamePlay.Player"))players.push_back(object);
    };
    for(const auto& f:read.fields(match)) {
        if(f.offset%8 || players.size()>=128 || attempted>=262144)continue;
        auto container=read.pointer(match+f.offset);auto name=read.objectName(container);
        bool dictionary=name.find("System.Collections.Generic.Dictionary`2") == 0;
        bool list=name.find("System.Collections.Generic.List`1") == 0;
        if(!dictionary && !list)continue;
        ++collections;
        std::uint64_t array=0;
        bool found=read.namedPointer(container,dictionary?"_entries":"_items",array);
        if(!found)found=read.namedPointer(container,dictionary?"entries":"items",array);
        if(!found || !array || read.objectName(array).find("[]")==std::string::npos)continue;
        auto length=read.pointer(array+0x18);
        if(!length || length>4096)continue;
        // A partial prefix only: eight bytes per allocated element fits reference lists
        // and Dictionary.Entry structs; deliberately does not assume an Entry stride.
        auto bytes=std::min<std::uint64_t>(length*8,32768);
        if(!readableRange(maps,array+0x20,bytes))continue;
        for(std::uint64_t off=0;off+8<=bytes && attempted<262144 && players.size()<128;off+=8) {
            addPlayer(read.pointer(array+0x20+off));attempted+=8;
        }
        out<<"Colecao candidata: "<<f.name<<" tipo="<<name<<" capacidade="<<length<<"; prefixo parcial="<<bytes<<" bytes"<<std::endl;
    }
    if((positions || capture) && manager && read.isA(manager,"COW.GamePlay.CameraControllerManager")) {
        auto selected=read.pointer(manager+0x20);
        managerCamera=read.isA(selected,"UnityEngine.Camera")?selected:0;
        printObject("Camera do getter do gerenciador (+0x20)",selected);
    }
    int positionResult=5;
    if(positions)positionResult=inspectPositions(memory,maps,base,players,local,managerCamera?managerCamera:camera);
    // Re-read roots to reject a snapshot taken across a match transition.
    bool stable=read.pointer(rootSlot)==rootClass && read.pointer(rootClass+0xb8)==staticFields
        && (read.pointer(staticFields)==game || read.pointer(staticFields+8)==game)
        && read.pointer(game+0x90)==match && read.pointer(match+0xd8)==local
        && read.pointer(local+0x690)==camera;
    out<<"Raizes mantidas durante snapshot: "<<(stable?"sim":"nao")<<std::endl;
    out<<"Colecoes candidatas: "<<collections<<"; referencias distintas do tipo Player: "<<players.size()<<std::endl;
    for(std::size_t i=0;i<std::min<std::size_t>(players.size(),16);++i)printObject("Referencia Player",players[i]);
    out<<"Referencias nao provam lista de adversarios ativos. Coordenadas e matrizes de projecao pendentes."<<std::endl;
    out<<"Nenhuma funcao remota foi chamada e nenhuma memoria do jogo foi escrita."<<std::endl;
    if(!stable)return 5;
    if(positions)return positionResult;
    if(!cameraValid && !managerCamera){out<<"SNAPSHOT PARCIAL: partida/jogador com tipos conferidos; camera pendente."<<std::endl;return 5;}
    if(capture){capture->game=game;capture->match=match;capture->local=local;capture->camera=managerCamera?managerCamera:camera;capture->players=players;}
    out<<"SNAPSHOT OK: partida, jogador local e referencia de camera com tipos conferidos. Ainda sem ESP."<<std::endl;
    return 0;
}
static int inspectGameObjects(bool positions=false) {
    if(positions){std::cout<<"COLETA EM 8 SEGUNDOS: volte ao treinamento e mantenha a camera parada."<<std::endl;
        for(unsigned i=8;i>0;--i){std::cout<<"Inicio em "<<i<<"s"<<std::endl;usleep(1000000);}}
    auto pid=findFreeFire();require(pid!=0,"Abra o treinamento e volte ao diagnostico.");
    std::uint64_t base=0;auto result=locateRuntimeCode(&base);if(result)return result;
    require(findFreeFire()==pid,"Processo mudou durante a busca. Repita o teste.");
    auto maps=processMaps(pid);ProcessMemory memory(pid,false);
    require(validatesCodeBase(memory,maps,base),"Codigo mudou durante a busca. Repita o teste.");
    return inspectRuntimeObjects(memory,maps,base,positions);
}


static const std::array<CodeProbe,9> boneProbes = {{{"Bone.spine",0x6f259a4ULL,{{0x74,0xf6,0x44,0xf9,0x74,0x2,0x0,0xb5,0x88,0xc8,0x1,0x90,0x8,0x7d,0x44,0xf9,0x60,0x2,0x40,0xf9,0x75,0xa2,0x27,0x91,0x14,0x1,0x40,0xf9,0xa4,0x4b,0xd0,0x94}}},{"Bone.hips",0x6f2f2bcULL,{{0x74,0x6,0x45,0xf9,0x74,0x2,0x0,0xb5,0x88,0xc0,0x1,0xf0,0x8,0x6d,0x47,0xf9,0x60,0x2,0x40,0xf9,0x75,0x22,0x28,0x91,0x14,0x1,0x40,0xf9,0x5e,0x25,0xd0,0x94}}},{"Bone.armL",0x708e10cULL,{{0x74,0xa6,0x50,0xf9,0x94,0x2,0x0,0xb5,0x48,0xbd,0x1,0xd0,0x8,0xb9,0x43,0xf9,0x60,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0x8,0x29,0x84,0x52,0x75,0x2,0x8,0x8b}}},{"Bone.armR",0x708e254ULL,{{0x74,0xaa,0x50,0xf9,0x94,0x2,0x0,0xb5,0x48,0xbd,0x1,0xd0,0x8,0xbd,0x43,0xf9,0x60,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0x8,0x2a,0x84,0x52,0x75,0x2,0x8,0x8b}}},{"Bone.legL",0x708de7cULL,{{0x74,0x9e,0x50,0xf9,0x94,0x2,0x0,0xb5,0xc8,0xbc,0x1,0xb0,0x8,0xa5,0x45,0xf9,0x60,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0x8,0x27,0x84,0x52,0x75,0x2,0x8,0x8b}}},{"Bone.legR",0x708dfc4ULL,{{0x74,0xa2,0x50,0xf9,0x94,0x2,0x0,0xb5,0x48,0xbd,0x1,0xf0,0x8,0xb5,0x43,0xf9,0x60,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0x8,0x28,0x84,0x52,0x75,0x2,0x8,0x8b}}},{"Bone.footL",0x708e39cULL,{{0x74,0xae,0x50,0xf9,0x94,0x2,0x0,0xb5,0xc8,0xbc,0x1,0x90,0x8,0xc1,0x45,0xf9,0x60,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0x8,0x2b,0x84,0x52,0x75,0x2,0x8,0x8b}}},{"Bone.footR",0x708e4e4ULL,{{0x74,0xb2,0x50,0xf9,0x94,0x2,0x0,0xb5,0xc8,0xbc,0x1,0x90,0x8,0xb9,0x45,0xf9,0x60,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0x8,0x2c,0x84,0x52,0x75,0x2,0x8,0x8b}}},{"Bone.neck",0x708e8bcULL,{{0x74,0xbe,0x50,0xf9,0x94,0x2,0x0,0xb5,0xa8,0xb5,0x1,0x90,0x8,0xa9,0x40,0xf9,0x60,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0x8,0x2f,0x84,0x52,0x75,0x2,0x8,0x8b}}}}};
static const std::array<std::uint32_t,9> boneOffsets = {{0x9e8,0xa08,0x2148,0x2150,0x2138,0x2140,0x2158,0x2160,0x2178}};

struct HeadReference {std::uint64_t node,klass,transform,transformClass,native;std::uint32_t offset;};
static HeadReference cacheBone(const ProcessMemory&,const std::vector<Mapping>&,std::uint64_t,std::uint32_t);
static HeadReference cacheHead(const ProcessMemory&,const std::vector<Mapping>&,std::uint64_t);
struct CachedPlayer {std::uint64_t object,klass,transform,native;std::uint32_t recycle;HeadReference head;std::string headError;std::array<HeadReference,9> bones;bool training;std::uint32_t botOffset;};
class QuietOutput {
    std::ostringstream sink;std::streambuf* previous;
public:QuietOutput():previous(std::cout.rdbuf(sink.rdbuf())){}~QuietOutput(){std::cout.rdbuf(previous);}
};
static std::vector<CachedPlayer> cachePlayers(const ProcessMemory& memory,const std::vector<Mapping>& maps,const RuntimeRefs& refs){
    ObjectReader read(memory,maps);std::vector<CachedPlayer> players;
    for(auto object:refs.players)try {
        if(object==refs.local || read.isA(object,"COW.GamePlay.Player_Watching"))continue;
        std::uint64_t transform=0;require(read.namedPointer(object,"m_CachedTransform",transform) && read.isA(transform,"UnityEngine.Transform"),"Player sem Transform");
        auto native=read.pointer(transform+0x10);require(readableRange(maps,native,0x48),"Player sem Transform nativo");
        std::uint32_t recycle=0;for(const auto& f:read.fields(object))if(f.name=="m_Recycle"){recycle=f.offset;break;}
        HeadReference head{};std::string headError;try{head=cacheHead(memory,maps,object);}catch(const std::exception& e){headError=e.what();}
        std::array<HeadReference,9> bones{};for(unsigned i=0;i<9;++i)try{bones[i]=cacheBone(memory,maps,object,boneOffsets[i]);}catch(const std::exception&){}
        std::uint32_t botOffset=0;for(const auto& field:read.fields(object))if(field.name=="IsClientBot"){botOffset=field.offset;break;}
        bool training=read.isA(object,"COW.GamePlay.Player_TrainingHumanTarget") || read.isA(object,"COW.GamePlay.Player_TrainingHumanTarget_Stand");
        players.push_back({object,read.pointer(object),transform,native,recycle,head,headError,bones,training,botOffset});
    }catch(const std::exception&){}
    return players;
}
static const std::array<CodeProbe,6> aimProbes = {{
{"Aim.Get",0x6f1c2b8ULL,{{0x60,0x16,0x46,0xbd,0x61,0x1a,0x46,0xbd,0x62,0x1e,0x46,0xbd,0x63,0x22,0x46,0xbd,0xfe,0x4f,0xc1,0xa8,0xc0,0x3,0x5f,0xd6,0x1,0x20,0xd0,0x94,0xfe,0x57,0xbe,0xa9}}},
{"Aim.Set",0x6f2c8b0ULL,{{0x6b,0x16,0x6,0xbd,0x6a,0x1a,0x6,0xbd,0x69,0x1e,0x6,0xbd,0x68,0x22,0x6,0xbd,0xf4,0x4f,0x43,0xa9,0xfe,0x57,0x42,0xa9,0xe9,0x23,0x41,0x6d,0xeb,0x2b,0xc4,0x6c}}},
{"Aim.CoreSet",0x6f2937cULL,{{0xe0,0x3,0x13,0xaa,0x6b,0x16,0x6,0xbd,0x6a,0x1a,0x6,0xbd,0x69,0x1e,0x6,0xbd,0x68,0x22,0x6,0xbd,0x9e,0x37,0xff,0x97,0xe0,0x3,0x0,0x36,0x33,0xc0,0x1,0xb0}}},
{"Dispatch.Root",0x882183cULL,{{0xf5,0x3b,0x1,0xd0,0x34,0x3,0x1,0xf0,0xa8,0xb6,0x5e,0x39,0x94,0x16,0x47,0xf9,0xf3,0x3,0x0,0x2a,0xc8,0x0,0x0,0x37,0x20,0x3,0x1,0xf0,0x0,0x14,0x47,0xf9}}},
{"Dispatch.Array",0x882187cULL,{{0x8,0x5c,0x40,0xf9,0x8,0x1,0x40,0xf9,0xe8,0x2,0x0,0xb4,0x9,0x19,0x40,0xb9,0x3f,0x1,0x13,0x6b,0xd,0x2,0x0,0x54,0x9,0xe0,0x40,0xb9,0xc9,0x0,0x0,0x35}}},
{"Dispatch.Slot",0x88218b0ULL,{{0x9,0x19,0x40,0xb9,0x3f,0x1,0x13,0x6b,0x69,0x1,0x0,0x54,0x8,0xcd,0x33,0x8b,0x8,0x11,0x40,0xf9,0x1f,0x1,0x0,0xf1,0xe0,0x7,0x9f,0x1a,0x2,0x0,0x0,0x14}}}
}};

static const std::array<CodeProbe,3> headProbes = {{{"Head.Reference",0x6fae488ULL,{{0x73,0x52,0x43,0xf9,0xd3,0x3,0x0,0xb4,0x68,0xbc,0x1,0xb0,0x8,0x75,0x41,0xf9,0x75,0x2,0x40,0xf9,0x14,0x1,0x40,0xf9,0xe0,0x3,0x15,0xaa,0xeb,0x28,0xce,0x94}}},{"Head.TransformNode",0x8251764ULL,{{0x0,0x8,0x40,0xf9,0xc0,0x3,0x5f,0xd6,0x1,0xc,0x1,0xf8,0x1b,0x4a,0x83,0x14,0xfe,0xf,0x1e,0xf8,0xf4,0x4f,0x1,0xa9,0xf3,0x3,0x1,0xaa,0xe1,0x3,0x1f,0xaa}}},{"Head.RuntimeNode",0x70d847cULL,{{0x0,0x8,0x40,0xf9,0xc0,0x3,0x5f,0xd6,0x1,0xc,0x1,0xf8,0xd5,0x2e,0xc9,0x14,0xfe,0x57,0xbe,0xa9,0xf4,0x4f,0x1,0xa9,0xf5,0xf5,0x1,0xb0,0xa8,0xce,0x45,0x39}}}}};

static const std::array<CodeProbe,6> fireProbes = {{{"Fire.Control",0x6f31864ULL,{{0x60,0x6a,0x42,0xf9,0xfe,0x4f,0xc1,0xa8,0xc0,0x3,0x5f,0xd6,0x99,0xca,0xcf,0x94,0xfe,0xf,0x1d,0xf8,0xf6,0x57,0x1,0xa9,0xf4,0x4f,0x2,0xa9,0x14,0x3,0x2,0xf0}}},{"Fire.Main",0x6f9d628ULL,{{0x41,0x2,0x80,0x52,0xe2,0x3,0x1f,0xaa,0xf3,0x3,0x0,0xaa,0x9,0x4,0x4b,0x94,0x0,0x2,0x0,0x37,0x81,0x4,0x80,0x52,0xe0,0x3,0x13,0xaa,0xe2,0x3,0x1f,0xaa}}},{"Fire.Aux",0x6f9d63cULL,{{0x81,0x4,0x80,0x52,0xe0,0x3,0x13,0xaa,0xe2,0x3,0x1f,0xaa,0x4,0x4,0x4b,0x94,0x60,0x1,0x0,0x37,0x81,0x5,0x80,0x52,0xe0,0x3,0x13,0xaa,0xe2,0x3,0x1f,0xaa}}},{"Fire.Joy",0x6f9d650ULL,{{0x81,0x5,0x80,0x52,0xe0,0x3,0x13,0xaa,0xe2,0x3,0x1f,0xaa,0xff,0x3,0x4b,0x94,0xc0,0x0,0x0,0x37,0x61,0x1,0x80,0x52,0xe0,0x3,0x13,0xaa,0xe2,0x3,0x1f,0xaa}}},{"Fire.Button",0x825e678ULL,{{0x88,0x16,0x40,0xf9,0xa8,0x1,0x0,0xb4,0x9,0x19,0x40,0xb9,0x3f,0x1,0x13,0x6b,0x69,0x1,0x0,0x54,0x8,0xc9,0x33,0x8b,0x0,0x21,0x40,0xbd,0x8,0x20,0x20,0x1e}}},{"Fire.Enable",0x8271174ULL,{{0x8,0x10,0x40,0xb9,0x9,0x18,0x40,0xb9,0x1f,0x1,0x0,0x71,0xea,0x7,0x9f,0x1a,0x1f,0x5,0x0,0x31,0xe8,0x17,0x9f,0x1a,0x3f,0x5,0x0,0x71,0x40,0x1,0x88,0x1a}}}}};
static bool readFirePressed(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t base,const RuntimeRefs& refs){
    for(const auto& probe:fireProbes)require(matchesProbe(memory,maps,base,probe),"Leitura do tiro: codigo divergente");
    ObjectReader read(memory,maps);
    require(read.pointer(refs.game+0x90)==refs.match && read.pointer(refs.match+0xd8)==refs.local && read.isA(refs.local,"COW.GamePlay.Player"),"Leitura do tiro: jogador mudou");
    bool controlField=false;for(const auto& field:read.fields(refs.local))if(field.offset==0x4d0){controlField=true;break;}
    require(controlField,"Campo de controle ausente");auto handler=read.pointer(refs.local+0x4d0);
    require(read.isA(handler,"GCommon.UserControlHandler"),"Controle do jogador indisponivel");
    std::uint64_t raw=0,enabled=0;require(read.namedPointer(handler,"m_RawDataCache",raw) && read.pointer(handler+0x28)==raw && read.objectName(raw)=="System.Single[]","Array dos botoes nao confirmado");
    require(read.namedPointer(handler,"m_IsButtonEnable",enabled) && read.pointer(handler+0x48)==enabled && read.isA(enabled,"GCommon.BitArrayBoolean"),"Estado dos botoes nao confirmado");
    auto length=read.pointer(raw+0x18);require(length>44 && length<=4096,"Array dos botoes fora dos limites");
    auto bits=memory.read32(enabled+0x10),mode=memory.read32(enabled+0x18);bool allowed=mode==1?bits!=0:bits==UINT32_MAX;
    bool pressed=false;for(auto index:{18ULL,36ULL,44ULL}){float value=0;memory.readBytes(raw+0x20+index*4,&value,4);require(std::isfinite(value),"Botao de tiro nao finito");pressed=pressed || value!=0;}
    require(read.pointer(refs.local+0x4d0)==handler && read.pointer(handler+0x28)==raw && read.pointer(handler+0x48)==enabled && read.pointer(refs.match+0xd8)==refs.local && read.pointer(refs.game+0x90)==refs.match,"Controles mudaram durante leitura");
    return allowed && pressed;
}

struct AimTarget {bool valid=false;Vec3 position{},origin{};double score=0.18*0.18,limit=0.18*0.18;std::uint64_t selected=0,preferred=0;unsigned inspected=0,heads=0,headFailures=0,sameTeam=0,behind=0,outside=0;std::string headError;};
static std::array<float,4> lookQuaternion(Vec3 origin,Vec3 target){
    double x=target.x-origin.x,y=target.y-origin.y,z=target.z-origin.z;
    require(finitePoint(target)&&finitePoint(origin)&&x*x+y*y+z*z>0.01,"Direcao de mira invalida");
    double yaw=std::atan2(x,z),pitch=-std::atan2(y,std::sqrt(x*x+z*z));
    double sy=std::sin(yaw/2),cy=std::cos(yaw/2),sp=std::sin(pitch/2),cp=std::cos(pitch/2);
    return {{static_cast<float>(sp*cy),static_cast<float>(cp*sy),static_cast<float>(-sp*sy),static_cast<float>(cp*cy)}};
}
static std::array<float,4> smoothQuaternion(const std::array<float,4>& current,std::array<float,4> desired){
    double n=0,dot=0;for(unsigned i=0;i<4;++i){require(std::isfinite(current[i]),"Rotacao atual nao finita");n+=current[i]*current[i];}
    require(std::fabs(n-1)<0.05,"Rotacao atual nao normalizada");
    for(unsigned i=0;i<4;++i)dot+=(current[i]/std::sqrt(n))*desired[i];
    if(dot<0){for(auto& v:desired)v=-v;dot=-dot;}
    double angle=2*std::acos(std::max(0.0,std::min(1.0,dot)));
    double t=std::min(0.25,angle>0?0.05235987756/angle:0.25),norm=0;std::array<float,4> result;
    for(unsigned i=0;i<4;++i){result[i]=static_cast<float>((1-t)*current[i]/std::sqrt(n)+t*desired[i]);norm+=result[i]*result[i];}
    require(norm>0.1,"Interpolacao invalida");for(auto& v:result)v=static_cast<float>(v/std::sqrt(norm));return result;
}
static void verifyAimPath(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t base){
    for(const auto& p:headProbes)require(matchesProbe(memory,maps,base,p),"Codigo de cabeca difere da referencia");
    for(const auto& p:aimProbes)require(matchesProbe(memory,maps,base,p),"Codigo de rotacao difere da referencia");
    ObjectReader read(memory,maps);auto klass=read.pointer(read.pointer(base+0xa888e28ULL));
    auto table=read.pointer(read.pointer(klass+0xb8));auto length=memory.read32(table+0x18);
    require(length<1000000,"Tabela de metodos invalida");
    for(auto slot:{900ULL,1209ULL,9760ULL,9761ULL})if(slot<length)require(read.pointer(table+0x20+slot*8)==0,"Rotacao usa uma substituicao de runtime; escrita suspensa");
}
static bool quaternionWritable(const std::vector<Mapping>& maps,std::uint64_t address){
    for(const auto& m:maps){if(address>=m.start && address<=UINT64_MAX-16 && address+16<=m.end && m.permissions.find('w')!=std::string::npos)return true;}
    return false;
}
static void applyAim(ProcessMemory& writable,const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t base,const RuntimeRefs& refs,const AimTarget& target,bool onFire=false){
    require(target.valid,"Nenhuma cabeca validada dentro do FOV");ObjectReader read(memory,maps);
    require(read.pointer(refs.game+0x90)==refs.match && read.pointer(refs.match+0xd8)==refs.local && read.isA(refs.local,"COW.GamePlay.Player"),"Jogador local mudou");
    verifyAimPath(memory,maps,base);
    require(quaternionWritable(maps,refs.local+0x614),"Campo de rotacao nao gravavel");
    std::array<float,4> current;memory.readBytes(refs.local+0x614,current.data(),16);
    auto replacement=smoothQuaternion(current,lookQuaternion(target.origin,target.position));
    require(read.pointer(refs.match+0xd8)==refs.local && read.pointer(refs.game+0x90)==refs.match,"Partida mudou antes da escrita");
    if(onFire){require(readFirePressed(memory,maps,base,refs),"Tiro solto; HS parado");replacement=lookQuaternion(target.origin,target.position);}
    writable.writeQuaternion(refs.local+0x614,current,replacement);
}
static double fovRadius(int value){return std::max(0,std::min(360,value))/720.0;}
static HeadReference cacheBone(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t player,std::uint32_t offset){
    ObjectReader read(memory,maps);bool headField=false;
    for(const auto& field:read.fields(player))if(field.offset==offset){headField=true;break;}
    require(headField,"Campo de cabeca ausente");
    auto node=read.pointer(player+offset);
    bool normal=read.isA(node,"GCommon.TransformNode") || read.isA(node,"TransformNode");
    bool runtime=read.isA(node,"BFOONIGAIFK");require(normal||runtime,("Tipo do no de cabeca nao confirmado: "+read.objectName(node)).c_str());
    std::uint64_t transform=0;
    require(read.namedPointer(node,runtime?"<PEGHKLKFBNK>k__BackingField":"<transform>k__BackingField",transform) && read.isA(transform,"UnityEngine.Transform"),"Transform de cabeca ausente");
    auto native=read.pointer(transform+0x10);require(readableRange(maps,native,0x48),"Transform nativo da cabeca ausente");
    return {node,read.pointer(node),transform,read.pointer(transform),native,offset};
}
static HeadReference cacheHead(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t player){return cacheBone(memory,maps,player,0x6a0);}
static Vec3 readCachedHead(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t player,const HeadReference& head){
    ObjectReader read(memory,maps);
    require(head.node && read.pointer(player+head.offset)==head.node && read.pointer(head.node)==head.klass,"Referencia de cabeca mudou");
    auto transform=read.pointer(head.node+0x10);require(transform,"Transform de cabeca ausente");
    require(transform==head.transform && read.pointer(transform)==head.transformClass && read.pointer(transform+0x10)==head.native,"Transform de cabeca mudou");
    auto position=readNativePose(memory,maps,head.native).position;
    require(read.pointer(player+head.offset)==head.node && read.pointer(head.node+0x10)==head.transform && read.pointer(head.transform+0x10)==head.native,"Cabeca mudou durante leitura");
    return position;
}
static __attribute__((unused)) Vec3 readHeadPosition(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t player){return readCachedHead(memory,maps,player,cacheHead(memory,maps,player));}

class AimControl {
    std::string pending;bool held=false;int selectedFov=130,selectedVisuals=5;
    std::chrono::steady_clock::time_point received{};
public:
    void poll(){
        char bytes[256];ssize_t count;
        while((count=read(STDIN_FILENO,bytes,sizeof(bytes)))>0){
            pending.append(bytes,static_cast<std::size_t>(count));
            if(pending.size()>1024){pending.clear();held=false;break;}
            std::size_t end;while((end=pending.find('\n'))!=std::string::npos){auto line=pending.substr(0,end);pending.erase(0,end+1);if(line.compare(0,4,"VIS ")==0){try{std::size_t used=0;int value=std::stoi(line.substr(4),&used);if(used==line.size()-4 && value>=0 && value<=7)selectedVisuals=value;}catch(const std::exception&){}continue;}if(line.compare(0,4,"FOV ")==0){try{std::size_t used=0;int value=std::stoi(line.substr(4),&used);if(used==line.size()-4 && value>=0 && value<=360)selectedFov=value;}catch(const std::exception&){}continue;}held=line=="ARM 1";received=std::chrono::steady_clock::now();}
        }
        if(count==0)held=false;
    }
    int fov() const{return selectedFov;}
    unsigned visuals() const{return static_cast<unsigned>(selectedVisuals);}
    bool active() const{return held && std::chrono::steady_clock::now()-received<std::chrono::milliseconds(250);}
};

struct ProjectedBone {double u,v;bool valid;ProjectedBone(double x=0,double y=0,bool ok=false):u(x),v(y),valid(ok){}};
struct EspPoint {double u,v,distance;ProjectedBone head;std::array<ProjectedBone,9> bones;EspPoint(double x,double y,double d):u(x),v(y),distance(d),head(),bones{} {}};
struct NearStats {unsigned bots=0,unknown=0;bool valid=false;};
static ProjectedBone screenBone(const Matrix4& view,const Matrix4& projection,Vec3 position){double u=0,v=0;if(!projectViewport(view,projection,position,u,v)||u<0||u>1||v<0||v>1)return {};return {u,v,true};}
static void emitRichFrame(const std::vector<EspPoint>& points,const NearStats& near){
    std::cout<<"FRAME2 "<<points.size()<<" "<<near.bots<<" "<<near.unknown<<" "<<(near.valid?1:0)<<std::fixed<<std::setprecision(6);
    for(const auto& p:points){std::cout<<" "<<p.u<<" "<<p.v<<" "<<p.distance<<" "<<p.head.u<<" "<<p.head.v<<" "<<(p.head.valid?1:0);for(const auto& b:p.bones)std::cout<<" "<<b.u<<" "<<b.v<<" "<<(b.valid?1:0);}
    std::cout<<std::endl;
}
static std::vector<EspPoint> collectEspFrame(const ProcessMemory& memory,const std::vector<Mapping>& maps,std::uint64_t unity,const RuntimeRefs& refs,const std::vector<CachedPlayer>& players,AimTarget* aim=nullptr,unsigned visuals=0,NearStats* near=nullptr){
    ObjectReader read(memory,maps);
    require(read.pointer(refs.game+0x90)==refs.match && read.pointer(refs.match+0xd8)==refs.local,"Partida mudou");
    Matrix4 view{},projection{};Vec3 cameraPosition{};readLiveCameraMatrices(memory,maps,unity,refs.camera,view,projection,cameraPosition);
    Vec3 localPosition{};
    if(near){*near=NearStats{};try{auto transform=read.pointer(refs.local+0x58);require(read.isA(transform,"UnityEngine.Transform"),"Transform local ausente");localPosition=readNativePose(memory,maps,read.pointer(transform+0x10)).position;near->valid=true;}catch(const std::exception&){} }
    auto localTeam=memory.read32(refs.local+0x430);
    std::vector<EspPoint> points;
    for(const auto& player:players)try {
        require(read.pointer(player.object)==player.klass && read.pointer(player.object+0x58)==player.transform && read.pointer(player.transform+0x10)==player.native,"Player mudou");
        if(player.recycle){unsigned char recycled=0;memory.readBytes(player.object+player.recycle,&recycled,1);if(recycled)continue;}
        auto position=readNativePose(memory,maps,player.native).position;double u=0,v=0;
        auto team=memory.read32(player.object+0x430);bool teammate=localTeam && team==localTeam;
        if(near && near->valid && !teammate){double x=position.x-localPosition.x,y=position.y-localPosition.y,z=position.z-localPosition.z;if(x*x+y*y+z*z<=10000){bool bot=player.training;if(player.botOffset){unsigned char value=0;memory.readBytes(player.object+player.botOffset,&value,1);bot=bot||value==1;}if(bot)++near->bots;else ++near->unknown;}}
        Vec3 headWorld{};bool headRead=false;std::string headFailure;
        if(aim || visuals)try{require(player.head.node,player.headError.empty()?"Referencia de cabeca pendente":player.headError.c_str());headWorld=readCachedHead(memory,maps,player.object,player.head);double x=headWorld.x-position.x,y=headWorld.y-position.y,z=headWorld.z-position.z;require(x*x+y*y+z*z<25,"Cabeca distante da raiz");headRead=true;}catch(const std::exception& e){headFailure=e.what();}
        if(aim){
            ++aim->inspected;
            try {
                require(headRead,headFailure.c_str());auto body=headWorld;double au=0,av=0;++aim->heads;
                if(localTeam && team==localTeam)++aim->sameTeam;
                else if(!projectViewport(view,projection,body,au,av))++aim->behind;
                else {
                    double dx=(au-0.5)*std::fabs(projection.v[5]/projection.v[0]),dy=av-0.5,score=dx*dx+dy*dy;
                    if(score>=aim->limit)++aim->outside;
                    else if(player.object==aim->preferred || ((aim->preferred==0 || aim->selected!=aim->preferred) && score<aim->score)){
                        aim->valid=true;aim->selected=player.object;aim->score=score;aim->position=body;aim->origin=cameraPosition;
                    }
                }
            }catch(const std::exception& e){++aim->headFailures;if(aim->headError.empty())aim->headError=e.what();}
        }

        if(!projectViewport(view,projection,position,u,v) || u<0 || u>1 || v<0 || v>1)continue;
        double x=position.x-cameraPosition.x,y=position.y-cameraPosition.y,z=position.z-cameraPosition.z,distance=std::sqrt(x*x+y*y+z*z);
        if(!std::isfinite(distance) || distance<0.1 || distance>5000)continue;
        if(teammate)continue;
        EspPoint point(u,v,distance);if(headRead)point.head=screenBone(view,projection,headWorld);
        if(visuals&2)for(unsigned i=0;i<9;++i)try{if(!player.bones[i].node)continue;auto world=readCachedHead(memory,maps,player.object,player.bones[i]);double x=world.x-position.x,y=world.y-position.y,z=world.z-position.z;if(x*x+y*y+z*z<25)point.bones[i]=screenBone(view,projection,world);}catch(const std::exception&){}
        points.push_back(point);if(points.size()==128)break;
    }catch(const std::exception&){}
    require(read.pointer(refs.game+0x90)==refs.match && read.pointer(refs.match+0xd8)==refs.local,"Partida mudou durante o frame");
    return points;
}

static std::string headDiagnostic(const AimTarget& target,int fov,std::size_t cached){
    std::ostringstream out;out<<"HS_DIAG tiro=1 fov="<<fov<<" cache="<<cached<<" avaliados="<<target.inspected<<" cabecas="<<target.heads<<" erros="<<target.headFailures<<" fora_fov="<<target.outside<<" atras="<<target.behind<<" mesma_equipe="<<target.sameTeam<<" alvo="<<(target.valid?"sim":"nao");
    if(!target.headError.empty())out<<" primeiro_erro="<<target.headError;
    return out.str();
}
static int streamEsp(){
    std::cout<<"STATUS Localizando codigo e objetos..."<<std::endl;
    auto pid=findFreeFire();require(pid!=0,"Abra o treinamento antes de ativar os controles");
    std::uint64_t base=0;require(locateRuntimeCode(&base)==0,"Base do jogo nao confirmada");
    auto initialMaps=processMaps(pid);ProcessMemory memory(pid,false);auto unity=locateUnityPositionCode(memory,initialMaps,base);verifyLiveViewCode(memory,initialMaps,unity);bool skeletonCodeVerified=true;for(const auto& probe:boneProbes)if(!matchesProbe(memory,initialMaps,base,probe))skeletonCodeVerified=false;if(!skeletonCodeVerified)std::cout<<"STATUS Esqueleto: codigo dos ossos nao confirmado"<<std::endl;
    struct Shared {std::mutex mutex;std::vector<Mapping> maps;RuntimeRefs refs;std::vector<CachedPlayer> players;std::string error="Localizando objetos";unsigned revision=0;bool ready=false;std::atomic<bool> stop{false},fast{false};} shared;
    std::thread updater([&]{
        while(!shared.stop){try{
            ProcessMemory reader(pid,false);
            auto maps=processMaps(pid);RuntimeRefs refs;std::ostringstream quiet;
            require(inspectRuntimeObjects(reader,maps,base,false,&refs,&quiet)==0,"Partida/jogador/camera indisponiveis");auto players=cachePlayers(reader,maps,refs);
            {std::lock_guard<std::mutex> lock(shared.mutex);shared.maps=std::move(maps);shared.refs=std::move(refs);shared.players=std::move(players);shared.ready=true;++shared.revision;}
        }catch(const std::exception& e){std::lock_guard<std::mutex> lock(shared.mutex);shared.ready=false;shared.error=e.what();++shared.revision;}
            for(unsigned i=0;i<(shared.fast?5U:20U) && !shared.stop;++i)usleep(100000);
        }
    });
    struct Join {Shared& s;std::thread& thread;~Join(){s.stop=true;if(thread.joinable())thread.join();}} join{shared,updater};
    int flags=fcntl(STDIN_FILENO,F_GETFL,0);if(flags>=0)fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK);AimControl control;
    RuntimeRefs refs;std::vector<Mapping> maps=initialMaps;std::vector<CachedPlayer> cached;
    std::uint64_t lockedTarget=0;
    unsigned revision=0,errors=0,frames=0;bool ready=false;std::string pendingError,aimStatus,lastHeadDiagnostic;
    auto lastProof=std::chrono::steady_clock::now(),lastStatus=lastProof,lastHeadReport=lastProof-std::chrono::seconds(2),lastTransientStatus=lastProof-std::chrono::seconds(2),lastEspPublish=lastProof-std::chrono::seconds(2);
    for(;;){auto started=std::chrono::steady_clock::now();control.poll();shared.fast=control.active();
        try{
            {std::lock_guard<std::mutex> lock(shared.mutex);if(revision!=shared.revision){revision=shared.revision;ready=shared.ready;pendingError=shared.error;if(ready){maps=shared.maps;refs=shared.refs;cached=shared.players;}}}
            require(ready,pendingError.empty()?"Localizando objetos":pendingError.c_str());
            if(started-lastProof>std::chrono::seconds(10)){require(findFreeFire()==pid && validatesCodeBase(memory,maps,base),"Processo/codigo mudou");verifyLiveViewCode(memory,maps,unity);lastProof=started;}
            bool firing=false;std::string state=control.active()?"HS pronto: aguardando botao de tiro":"HS desligado";
            if(control.active())try{firing=readFirePressed(memory,maps,base,refs);}catch(const std::exception& e){state=e.what();}
            if(!firing){lockedTarget=0;lastHeadDiagnostic.clear();}
            int frameFov=control.fov();AimTarget target;target.score=target.limit=std::pow(fovRadius(frameFov),2);target.preferred=lockedTarget;
            bool publish=started-lastEspPublish>=std::chrono::microseconds(33333);NearStats near;auto points=collectEspFrame(memory,maps,unity,refs,cached,firing?&target:nullptr,publish?(control.visuals()&(skeletonCodeVerified?7U:5U)):0,publish?&near:nullptr);errors=0;
            if(firing){
                auto report=headDiagnostic(target,frameFov,cached.size())+" coleta_us="+std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count());
                if(lastHeadDiagnostic.empty() || started-lastHeadReport>=std::chrono::seconds(1)){std::cout<<report<<std::endl;lastHeadDiagnostic=report;lastHeadReport=started;}
            }
            control.poll();
            if(firing && !target.valid){state=target.headError.empty()?"HS: nenhuma cabeca elegivel no FOV":("HS: "+target.headError);}
            if(firing && target.valid && control.active() && control.fov()==frameFov)try{ProcessMemory writer(pid,true);applyAim(writer,memory,maps,base,refs,target,true);lockedTarget=target.selected;state="HS: acompanhando cabeca durante o tiro";}catch(const std::exception& e){lockedTarget=0;state=e.what();}
            if(state!=aimStatus){aimStatus=state;std::cout<<"AIMSTATUS "<<state<<std::endl;}
            if(publish){emitRichFrame(points,near);lastEspPublish=started;++frames;}
            auto elapsed=std::chrono::steady_clock::now()-lastStatus;if(publish && elapsed>=std::chrono::seconds(1)){
                double fps=frames/std::chrono::duration<double>(elapsed).count();std::cout<<"STATUS ESP: "<<points.size()<<" pontos | "<<std::setprecision(1)<<fps<<" fps";if(control.visuals()&2){unsigned joints=0;for(const auto& point:points)for(const auto& joint:point.bones)if(joint.valid)++joints;std::cout<<" | ossos="<<joints;}std::cout<<std::endl;frames=0;lastStatus=std::chrono::steady_clock::now();}
        }catch(const SnapshotChanged& e){
            errors=0;std::cout<<"FRAME 0"<<std::endl;
            if(started-lastTransientStatus>=std::chrono::seconds(1)){std::cout<<"STATUS Atualizando leitura em movimento: "<<e.what()<<std::endl;lastTransientStatus=started;}
        }catch(const std::exception& e){std::cout<<"FRAME 0\nSTATUS Aguardando dados: "<<e.what()<<std::endl;if(++errors>=60)return 6;}
        if(!std::cout.good())return 0;
        auto micros=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        auto interval=errors?500000LL:(control.active()?16667LL:33333LL);if(micros<interval)usleep(static_cast<useconds_t>(interval-micros));
    }
}

int main(int argc, char** argv) {
    std::cout << "RDZIM_NATIVE=2.7\n";
    try {
        if (argc == 2 && std::string(argv[1]) == "--stream-esp") return streamEsp();
        if (argc == 2 && std::string(argv[1]) == "--self-test") return selfTest();
        if (argc == 2 && std::string(argv[1]) == "--inspect-freefire") return inspectFreeFire();
        if (argc == 2 && std::string(argv[1]) == "--inspect-positions") return inspectGameObjects(true);
        if (argc == 2 && std::string(argv[1]) == "--inspect-objects") return inspectGameObjects();
        if (argc == 2 && std::string(argv[1]) == "--locate-runtime") return locateRuntimeCode();
        bool writeMode = argc == 6 && std::string(argv[1]) == "write32";
        bool readMode = argc == 4 && std::string(argv[1]) == "read32";
        if (!readMode && !writeMode) {
            std::cerr << "Usage:\n  memory_tool --self-test\n  memory_tool read32 PID ADDRESS\n  memory_tool write32 PID ADDRESS EXPECTED REPLACEMENT\nNumbers: decimal or 0x-prefixed hexadecimal.\n";
            return 2;
        }
        auto pid = number(argv[2]);
        if (!pid || pid > static_cast<std::uint64_t>(std::numeric_limits<pid_t>::max())) throw std::runtime_error("PID out of range");
        auto address = number(argv[3]);
        ProcessMemory memory(static_cast<pid_t>(pid), writeMode);
        if (writeMode) memory.write32(address, word(argv[4]), word(argv[5]));
        auto value = memory.read32(address);
        std::cout << "value=0x" << std::hex << std::setw(8) << std::setfill('0') << value << std::dec << " (" << value << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
