#define main native_tool_main
#include "../native/native-v311.cpp"
#undef main
#include <cassert>
int main(){
    char self[64]={};auto n=readlink("/proc/self",self,sizeof(self)-1);assert(n>0);auto pid=static_cast<pid_t>(std::stol(std::string(self,n)));
    auto stamp=processStamp(pid);assert(stamp.start>0);requireSameProcess(stamp);bool caught=false;try{requireSameProcess({pid,stamp.start+1});}catch(const memory_guard::WriteFault&){caught=true;}assert(caught);
    alignas(64) std::array<float,4> fixture{{0,0,0,1}},desired{{0,0.70710677f,0,0.70710677f}},expected=fixture,got{};
    auto address=reinterpret_cast<std::uint64_t>(fixture.data());ProcessMemory memory(pid,true);auto maps=processMaps(pid);assert(quaternionWritable(maps,address));assert(currentQuaternionWritable(pid,address));
    assert(memory.writeQuaternion(address,expected,desired));memory.readBytes(address,got.data(),16);assert(got==desired);assert(!memory.writeQuaternion(address,desired,desired));
    caught=false;try{memory.writeQuaternion(address,desired,expected,[](){return false;});}catch(const std::exception&){caught=true;}assert(caught);memory.readBytes(address,got.data(),16);assert(got==desired);
    caught=false;try{ProcessMemory ro(pid,false);ro.writeQuaternion(address,desired,expected);}catch(const std::exception&){caught=true;}assert(caught);
    for(const auto& flags:{"r--p","rwxp","rw-s","--wp"})assert(!quaternionWritable({{4096,8192,0,flags,"fixture"}},4100));
    assert(quaternionWritable({{4096,8192,0,"rw-p","fixture"}},4100));assert(!quaternionWritable({{4096,8192,0,"rw-p","fixture"}},8180));assert(!quaternionWritable({{4096,8192,0,"rw-p","fixture"}},4101));
    int p[2];assert(pipe(p)==0);int original=dup(0);dup2(p[0],0);fcntl(0,F_SETFL,O_NONBLOCK);
    AimControl control;const char* arm="ARM 1\n";assert(write(p[1],arm,6)==6);control.poll();assert(control.active());
    assert(write(p[1],"STOP\nARM 1\n",11)==11);control.poll();assert(control.stopped()&&!control.active());
    AimControl eof;close(p[1]);eof.poll();assert(eof.stopped()&&!eof.active());dup2(original,0);close(original);close(p[0]);
    std::cout<<"PASS: real process-memory quaternion transaction, read-only/gate rejection, private-data range, process identity and STOP/EOF shutdown"<<std::endl;
}
