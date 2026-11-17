#define main native_tool_main
#include "../native/native-v311.cpp"
#undef main
#include <cassert>
int main(){
 auto forward=lookQuaternion({0,0,0},{0,0,10});assert(memory_guard::equivalent(forward,{{0,0,0,1}}));
 auto turned=lookQuaternion({0,0,0},{10,0,0});float q[4];for(unsigned i=0;i<4;i++)q[i]=turned[i];auto direction=rotatePoint(q,{0,0,1});assert(std::fabs(direction.x-1)<1e-5&&std::fabs(direction.y)<1e-5&&std::fabs(direction.z)<1e-5);
 EspPoint first(.2,.7,12),second(.75,.8,40);first.id=0xabc;first.head={.2,.3,true};first.hp=75;first.maxHp=200;first.hpValid=true;first.nameHex="4c75636173";second.id=0xdef;NearStats near;near.unknown=1;near.valid=true;emitRichFrame({first,second},near);
}
