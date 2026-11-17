#include "../native/memory-write-guard.hpp"
#include <cassert>
#include <limits>
#include <iostream>
using namespace memory_guard;
template<class Action> static void rejected(Action action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}assert(failed);}
int main(){
    Quaternion id{{0,0,0,1}},turned{{0,0.70710677f,0,0.70710677f}},negative{{0,0,0,-1}},value=id;
    int writes=0,reads=0;bool allow=true;
    auto read=[&](Quaternion& out){++reads;out=value;};auto write=[&](const Quaternion& in){++writes;value=in;return 16;};auto gate=[&](){return allow;};
    for(int i=0;i<10000;i++){assert(!transaction(id,id,read,write,gate));}
    assert(writes==0);
    assert(!transaction(id,negative,read,write,gate));assert(writes==0);
    assert(transaction(id,turned,read,write,gate));assert(writes==1&&value==turned);
    rejected([&](){transaction(id,turned,read,write,gate);});assert(writes==1);
    allow=false;rejected([&](){transaction(turned,id,read,write,gate);});assert(writes==1);allow=true;
    Quaternion invalid=id;invalid[0]=std::numeric_limits<float>::quiet_NaN();rejected([&](){transaction(turned,invalid,read,write,gate);});assert(writes==1);
    invalid={{0,0,0,2}};rejected([&](){transaction(turned,invalid,read,write,gate);});assert(writes==1);
    value=id;int attempts=0;bool fault=false;
    try{transaction(id,turned,read,[&](const Quaternion&){++attempts;return 8;},gate);}catch(const WriteFault&){fault=true;}assert(fault&&attempts==1);
    fault=false;try{transaction(id,turned,read,[&](const Quaternion&){++attempts;return 16;},gate);}catch(const WriteFault&){fault=true;}assert(fault&&attempts==2);
    int stage=0;fault=false;try{transaction(id,turned,[&](Quaternion& out){if(++stage==3)throw std::runtime_error("readback");out=id;},write,gate);}catch(const WriteFault&){fault=true;}assert(fault);
    value=id;int writesBefore=writes;rejected([&](){transaction(id,turned,read,write,[&](){value=negative;return true;});});assert(writes==writesBefore);
    Budget budget;auto now=Clock::time_point(std::chrono::seconds(10));assert(budget.ready(now));budget.written(now);assert(!budget.ready(now+std::chrono::microseconds(16666)));assert(budget.ready(now+std::chrono::microseconds(16667)));assert(!budget.ready(now-std::chrono::seconds(1)));
    budget.fault(7);assert(!budget.ready(now+std::chrono::hours(1)));budget.observeDisarm(7);assert(budget.faulted());budget.observeDisarm(8);assert(!budget.faulted()&&budget.ready(now));
    assert(fresh(now,now+std::chrono::milliseconds(100)));assert(!fresh(now,now+std::chrono::milliseconds(101)));assert(!fresh(now,now-std::chrono::milliseconds(1)));assert(!fresh(Clock::time_point{},now));
    assert(singlePage(0x614,16,4096));assert(!singlePage(4092,16,4096));assert(!singlePage(UINT64_MAX-3,16,4096));assert(!singlePage(4096,16,0));
    std::string stat="123 (name ) with spaces) S";for(int field=4;field<=21;field++)stat+=" 0";stat+=" 456 0";assert(startTicks(stat,123)==456);rejected([&](){startTicks(stat,124);});rejected([&](){startTicks("123 (broken) S",123);});rejected([&](){startTicks("abc",123);});
    std::cout<<"PASS: 10000 no-op writes skipped, equivalent signs, stale/invalid/gated rejection, partial/readback faults, 60 Hz ceiling, explicit rearm and snapshot age"<<std::endl;
}
