#define main native_tool_main
#include "../native/native-v311.cpp"
#undef main
#include <cassert>
#include <memory>
struct Fake {
    std::vector<std::unique_ptr<unsigned char[]>> blocks;
    std::uint64_t alloc(std::size_t n){auto p=std::make_unique<unsigned char[]>(n);auto a=reinterpret_cast<std::uint64_t>(p.get());blocks.push_back(std::move(p));return a;}
    template<class T> void put(std::uint64_t a,T v){std::memcpy(reinterpret_cast<void*>(a),&v,sizeof(v));}
    std::uint64_t text(const std::string& s){auto a=alloc(s.size()+1);std::memcpy(reinterpret_cast<void*>(a),s.c_str(),s.size()+1);return a;}
    std::uint64_t klass(const char* ns,const char* name,const std::vector<std::pair<std::string,unsigned>>& fields){
        auto c=alloc(0x180);put(c+0x10,text(name));put(c+0x18,text(ns));auto table=alloc((fields.size()+1)*32);put(c+0x80,table);
        for(unsigned i=0;i<fields.size();i++){auto at=table+i*32,t=alloc(16);put(at,text(fields[i].first));put(at+8,t);put(at+16,c);put(at+24,fields[i].second);put(t+8,std::uint32_t((fields[i].first=="BDHGDLFDHAL"?0x0e:0x12)<<16));}return c;
    }
    std::uint64_t object(std::uint64_t c,std::size_t n){auto a=alloc(n);put(a,c);return a;}
};
int main(){
    Fake f;
    auto pc=f.klass("COW.GamePlay","FixturePlayer",{{"m_PRIDataPool",0x70},{"BDHGDLFDHAL",0x490}});
    auto player=f.object(pc,0x500);
    auto poolClass=f.klass("GCommon","PRIDataPool",{{"m_Datas",0x10},{"m_MaxVarCount",0x18}});
    auto arrayClass=f.klass("GCommon","ReplicationData[]",{});
    auto itemClass=f.klass("GCommon","ReplicationData",{{"Value",0x18}});
    auto pool=f.object(poolClass,0x40),array=f.object(arrayClass,0x40),current=f.object(itemClass,0x30),maximum=f.object(itemClass,0x30);
    f.put(player+0x70,pool);f.put(pool+0x10,array);f.put(pool+0x18,std::uint32_t(2));f.put(array+0x18,std::uint64_t(2));f.put(array+0x20,current);f.put(array+0x28,maximum);f.put(current+0x18,std::uint16_t(75));f.put(maximum+0x18,std::uint16_t(200));
    auto sc=f.klass("System","String",{}),name=f.object(sc,0x40);f.put(name+0x10,std::uint32_t(6));const std::uint16_t chars[]={'L','u','c','a','s',0xe3};std::memcpy(reinterpret_cast<void*>(name+0x14),chars,sizeof(chars));f.put(player+0x490,name);
    char self[64]={};auto length=readlink("/proc/self",self,sizeof(self)-1);assert(length>0);auto procPid=static_cast<pid_t>(std::stol(std::string(self,length)));ProcessMemory memory(procPid,false);auto maps=processMaps(procPid);int hp=0,max=0;
    assert(readPlayerHealth(memory,maps,player,hp,max)&&hp==75&&max==200);
    assert(playerNameHex(memory,maps,player)=="4c75636173c3a3");
    f.put(current+0x18,std::uint16_t(0));assert(readPlayerHealth(memory,maps,player,hp,max)&&hp==0);
    f.put(current+0x18,std::uint16_t(201));assert(!readPlayerHealth(memory,maps,player,hp,max));
    f.put(current+0x18,std::uint16_t(75));f.put(maximum+0x18,std::uint16_t(0));assert(!readPlayerHealth(memory,maps,player,hp,max));f.put(maximum+0x18,std::uint16_t(200));
    f.put(array+0x18,std::uint64_t(1));assert(!readPlayerHealth(memory,maps,player,hp,max));f.put(array+0x18,std::uint64_t(2));
    auto uc=f.klass("GCommon","PRIDataPoolUnsafe",{{"m_Datas",0x10},{"m_MaxVarCount",0x18}}),ac=f.klass("GCommon","ReplicationDataUnsafe[]",{}),ic=f.klass("GCommon","ReplicationDataUnsafe",{{"Value",0x18}});
    auto up=f.object(uc,0x40),ua=f.object(ac,0x40),u0=f.object(ic,0x30),u1=f.object(ic,0x30),v0=f.alloc(8),v1=f.alloc(8);
    f.put(player+0x70,up);f.put(up+0x10,ua);f.put(up+0x18,std::uint32_t(2));f.put(ua+0x18,std::uint64_t(2));f.put(ua+0x20,u0);f.put(ua+0x28,u1);f.put(u0+0x18,v0);f.put(u1+0x18,v1);f.put(v0,std::uint16_t(100));f.put(v1,std::uint16_t(250));maps=processMaps(procPid);
    assert(readPlayerHealth(memory,maps,player,hp,max)&&hp==100&&max==250);
    f.put(u0+0x18,std::uint64_t(1));assert(!readPlayerHealth(memory,maps,player,hp,max));
    int pipes[2];assert(pipe(pipes)==0);int old=dup(0);dup2(pipes[0],0);fcntl(0,F_SETFL,O_NONBLOCK);AimControl control;
    auto command=[&](const char* s){assert(write(pipes[1],s,std::strlen(s))==ssize_t(std::strlen(s)));control.poll();};
    command("TARGET 1\nFOV 360\nVIS 15\nARM 1\n");assert(control.target()==1&&control.fov()==360&&control.visuals()==15&&control.active());
    command("TARGET 2\n");assert(control.target()==2&&!control.active());command("TARGET 9\nFOV -1\n");assert(control.target()==2&&control.fov()==360);command("ARM 0\n");assert(!control.active());dup2(old,0);close(old);close(pipes[0]);close(pipes[1]);
    std::cout<<"PASS: managed/indirect HP, real Unicode name, invalid HP/pointers, target/FOV/enable protocol"<<std::endl;
}
