#include <sys/sysinfo.h>
#include "debug.h"
template<typename TFunction, typename... TArgs>
double getRunTime(TFunction func,TArgs... args){
    auto start=1.*clock()/CLOCKS_PER_SEC;
    func(args...);
    auto end=1.*clock()/CLOCKS_PER_SEC;
    return end-start;
}

unsigned long getFreeMemory(){
    struct sysinfo tmp;
    if(sysinfo(&tmp)==0){
        return tmp.freeram/(1024*1024);
    }else{
        puts("get system info failed");
        return 0;
    }
}