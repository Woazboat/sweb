#include "assert.h"

#include "Scheduler.h"
#include "SystemState.h"
#include "Thread.h"
#include "debug_bochs.h"
#include "kprintf.h"

#include "ArchInterrupts.h"

extern "C" void halt();

[[noreturn]] void sweb_assert(const char *condition, uint32 line, const char* file, const char* function)
{
  ArchInterrupts::disableInterrupts();
  system_state = KPANIC;
  kprintfd("KERNEL PANIC: Assertion %s failed in File:Line %s:%d Function %s\n", condition, file, line, function);
  if (currentThread != 0)
    currentThread->printBacktrace(false);
  while(1);
}
