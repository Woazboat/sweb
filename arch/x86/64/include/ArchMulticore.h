#pragma once

#include "APIC.h"
#include "AtomicMpScQueue.h"
#include "Cpu.h"
#include "IdleThread.h"
#include "Mutex.h"
#include "RemoteFunctionCall.h"
#include "SMP.h"
#include "SegmentUtils.h"
#include "X2Apic.h"
#include "paging-definitions.h"

#include "ArchCpuLocalStorage.h"

#include "types.h"

#include "EASTL/atomic.h"
#include "EASTL/vector.h"

#define CPU_STACK_SIZE 4*PAGE_SIZE


class ArchCpu : public Cpu
{
public:
    ArchCpu();

    Apic* lapic;

    IrqDomain& rootIrqDomain();

private:
    IrqDomain** root_domain_ptr;
};


extern cpu_local Apic* cpu_lapic;
extern cpu_local char cpu_stack[CPU_STACK_SIZE];
extern cpu_local TSS cpu_tss;
extern cpu_local IdleThread* idle_thread;



class Allocator;

class ArchMulticore
{
  public:
    static void initialize();

    static void reservePages(Allocator& alloc);

    static void startOtherCPUs();
    static void stopAllCpus();
    static void stopOtherCpus();

    static void startAP(uint32_t apic_id, size_t entry_addr);

    static void sendFunctionCallMessage(ArchCpu& cpu, RemoteFunctionCallMessage* fcall_message);
    static void notifyMessageAvailable(ArchCpu& cpu);

    static void initApplicationProcessorCpu();
    static void initCpuLocalData(bool boot_cpu = false);

    static char* cpuStackTop();

    static inline size_t AP_STARTUP_PADDR = 0x1000;

  private:
    static void initCpuLocalGDT(GDT& template_gdt);
    static void initCpuLocalTSS(size_t boot_stack_top);
    static void prepareAPStartup(size_t entry_addr);

    [[noreturn]] static void waitForSystemStart();
};
