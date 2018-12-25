#include <functional>
#include <time.h>
template<typename TFunction, typename... TArgs>
int getRunTime(TFunction func,TArgs... args);
unsigned long getFreeMemory();