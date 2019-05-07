#include "InterruptUtils.h"

#include "ArchSerialInfo.h"
#include "BDManager.h"
#include "new.h"
#include "ports.h"
#include "ArchMemory.h"
#include "ArchThreads.h"
#include "ArchCommon.h"
#include "Console.h"
#include "Terminal.h"
#include "kprintf.h"
#include "Scheduler.h"
#include "debug_bochs.h"
#include "offsets.h"
#include "kstring.h"

#include "SerialManager.h"
#include "KeyboardManager.h"
#include "panic.h"

#include "Thread.h"
#include "ArchInterrupts.h"
#include "ArchMulticore.h"
#include "backtrace.h"

#include "SWEBDebugInfo.h"
#include "Loader.h"
#include "Syscall.h"
#include "paging-definitions.h"
#include "PageFaultHandler.h"

#include "8259.h"

#define LO_WORD(x) (((uint32)(x)) & 0x0000FFFFULL)
#define HI_WORD(x) ((((uint32)(x)) >> 16) & 0x0000FFFFULL)
#define LO_DWORD(x) (((uint64)(x)) & 0x00000000FFFFFFFFULL)
#define HI_DWORD(x) ((((uint64)(x)) >> 32) & 0x00000000FFFFFFFFULL)

#define TYPE_TRAP_GATE      15 // trap gate, i.e. IF flag is *not* cleared
#define TYPE_INTERRUPT_GATE 14 // interrupt gate, i.e. IF flag *is* cleared

#define DPL_KERNEL_SPACE     0 // kernelspace's protection level
#define DPL_USER_SPACE       3 // userspaces's protection level

#define SYSCALL_INTERRUPT 0x80 // number of syscall interrupt


// --- Pagefault error flags.
//     PF because/in/caused by/...

#define FLAG_PF_PRESENT     0x01 // =0: pt/page not present
                                 // =1: of protection violation

#define FLAG_PF_RDWR        0x02 // =0: read access
                                 // =1: write access

#define FLAG_PF_USER        0x04 // =0: supervisormode (CPL < 3)
                                 // =1: usermode (CPL == 3)

#define FLAG_PF_RSVD        0x08 // =0: not a reserved bit
                                 // =1: a reserved bit

#define FLAG_PF_INSTR_FETCH 0x10 // =0: not an instruction fetch
                                 // =1: an instruction fetch (need PAE for that)

extern "C" void arch_dummyHandler();
extern "C" void arch_dummyHandlerMiddle();

IDTR InterruptUtils::idtr;

InterruptGateDesc* InterruptUtils::idt;

InterruptGateDesc::InterruptGateDesc(uint64 offset, uint8 dpl) :
  segment_selector(KERNEL_CS),
  ist(0),
  zeros(0),
  type(TYPE_INTERRUPT_GATE),
  zero_1(0),
  dpl(dpl),
  present(1),
  reserved(0)
{
  setOffset(offset);
}

void InterruptGateDesc::setOffset(uint64 offset)
{
  offset_ld_lw = LO_WORD(LO_DWORD( offset ));
  offset_ld_hw = HI_WORD(LO_DWORD( offset ));
  offset_hd =    HI_DWORD(         offset );
}

void IDTR::load()
{
  debug(A_INTERRUPTS, "Loading IDT, base: %zx, limit: %x\n", base, limit);
  asm volatile("lidt (%0) ": :"q" (this));
}

void InterruptUtils::initialise()
{
  uint32 num_handlers = 0;
  for (uint32 i = 0; handlers[i].offset != 0; ++i)
  {
    num_handlers = Max(handlers[i].number, num_handlers);
  }
  num_handlers += 1;
  idt = new InterruptGateDesc[num_handlers];
  size_t dummy_handler_sled_size = (((size_t) arch_dummyHandlerMiddle) - (size_t) arch_dummyHandler);
  assert((dummy_handler_sled_size % 128) == 0 && "cannot handle weird padding in the kernel binary");
  dummy_handler_sled_size /= 128;

  for (uint32 i = 0; i < num_handlers; ++i)
  {
    idt[i] = InterruptGateDesc(((size_t)arch_dummyHandler) + i*dummy_handler_sled_size, DPL_KERNEL_SPACE);
  }

  uint32 j = 0;
  while(handlers[j].offset != 0)
  {
    assert(handlers[j].number < num_handlers);
    uint8 dpl = (handlers[j].number == SYSCALL_INTERRUPT) ? DPL_USER_SPACE :
                                                            DPL_KERNEL_SPACE;
    idt[handlers[j].number] = InterruptGateDesc((size_t)handlers[j].offset, dpl);
    ++j;
  }

  if(A_INTERRUPTS & OUTPUT_ENABLED)
  {
    for (uint32 i = 0; i < num_handlers; ++i)
    {
      debug(A_INTERRUPTS,
            "%x -- offset = %p, offset_ld_lw = %x, offset_ld_hw = %x, offset_hd = %x, ist = %x, present = %x, segment_selector = %x, type = %x, dpl = %x\n", i, handlers[i].offset,
            idt[i].offset_ld_lw, idt[i].offset_ld_hw,
            idt[i].offset_hd, idt[i].ist,
            idt[i].present, idt[i].segment_selector,
            idt[i].type, idt[i].dpl);
    }
  }

  idtr.base = (pointer) idt;
  idtr.limit = sizeof(InterruptGateDesc) * num_handlers - 1;
  idtr.load();
}

extern SWEBDebugInfo const *kernel_debug_info;


extern "C" void arch_contextSwitch();

void beginIRQ(size_t irq_num)
{
  ArchInterrupts::startOfInterrupt(irq_num);
}

void endIRQ(size_t irq_num)
{
  ArchInterrupts::endOfInterrupt(irq_num);
}

// Standard ISA IRQs
// 0 	Programmable Interrupt Timer Interrupt
// 1 	Keyboard Interrupt
// 2 	Cascade (used internally by the two PICs. never raised)
// 3 	COM2 (if enabled)
// 4 	COM1 (if enabled)
// 5 	LPT2 (if enabled)
// 6 	Floppy Disk
// 7 	LPT1 / Unreliable "spurious" interrupt (usually)
// 8 	CMOS real-time clock (if enabled)
// 9 	Free for peripherals / legacy SCSI / NIC
// 10 	Free for peripherals / SCSI / NIC
// 11 	Free for peripherals / SCSI / NIC
// 12 	PS2 Mouse
// 13 	FPU / Coprocessor / Inter-processor
// 14 	Primary ATA Hard Disk
// 15 	Secondary ATA Hard Disk

extern "C" void arch_irqHandler_0();
extern "C" void irqHandler_0()
{
  debug(A_INTERRUPTS, "IRQ 0 called by CPU %zx\n", ArchMulticore::getCpuID());
  beginIRQ(0);
  ArchCommon::drawHeartBeat();

  Scheduler::instance()->incTicks();

  ArchCommon::callWithStack(ArchMulticore::cpuStackTop(),
    []()
    {
      Scheduler::instance()->schedule();
      ((char*)ArchCommon::getFBPtr())[1 + ArchMulticore::getCpuID()*2] =
              ((currentThread == &idle_thread ? (Console::RED << 4) :
                (Console::BRIGHT_BLUE << 4)) |
               Console::BRIGHT_WHITE);
      endIRQ(0);
      arch_contextSwitch();
      assert(false);
    });
}

extern "C" void arch_irqHandler_65();
extern "C" void irqHandler_65()
{
  ArchCommon::callWithStack(ArchMulticore::cpuStackTop(),
    []()
    {
      Scheduler::instance()->schedule();
      ((char*)ArchCommon::getFBPtr())[1 + ArchMulticore::getCpuID()*2] =
              ((currentThread == &idle_thread ? (Console::RED << 4) :
                (Console::BRIGHT_BLUE << 4)) |
               Console::BRIGHT_WHITE);

      arch_contextSwitch();
      assert(false);
    });
}

extern "C" void arch_pageFaultHandler();
extern "C" void pageFaultHandler(uint64 address, uint64 error, uint64 ip)
{
  PageFaultHandler::enterPageFault(address, ip, error & FLAG_PF_USER,
                                   error & FLAG_PF_PRESENT,
                                   error & FLAG_PF_RDWR,
                                   error & FLAG_PF_INSTR_FETCH);
  if (currentThread->switch_to_userspace_)
    arch_contextSwitch();
  else
    asm volatile ("movq %%cr3, %%rax; movq %%rax, %%cr3;" ::: "%rax");
}

extern "C" void arch_irqHandler_1();
extern "C" void irqHandler_1()
{
  beginIRQ(1);
  debug(A_INTERRUPTS, "Interrupt vector %u (%x) called\n", 1, 1 + 0x20);
  KeyboardManager::instance()->serviceIRQ( );
  endIRQ(1);
}

extern "C" void arch_irqHandler_3();
extern "C" void irqHandler_3()
{
  kprintfd( "IRQ 3 called\n" );
  beginIRQ(3);
  debug(A_INTERRUPTS, "Interrupt vector %u (%x) called\n", 3, 3 + 0x20);
  SerialManager::getInstance()->service_irq( 3 );
  endIRQ(3);
  kprintfd( "IRQ 3 ended\n" );
}

extern "C" void arch_irqHandler_4();
extern "C" void irqHandler_4()
{
  debug(A_INTERRUPTS, "Interrupt vector %u (%x) called\n", 4, 4 + 0x20);
  kprintfd( "IRQ 4 called\n" );
  beginIRQ(4);
  SerialManager::getInstance()->service_irq( 4 );
  endIRQ(4);
  kprintfd( "IRQ 4 ended\n" );
}

extern "C" void arch_irqHandler_6();
extern "C" void irqHandler_6()
{
  debug(A_INTERRUPTS, "Interrupt vector %u (%x) called\n", 6, 6 + 0x20);
  kprintfd( "IRQ 6 called\n" );
  kprintfd( "IRQ 6 ended\n" );
}

extern "C" void arch_irqHandler_9();
extern "C" void irqHandler_9()
{
  debug(A_INTERRUPTS, "Interrupt vector %u (%x) called\n", 9, 9 + 0x20);
  kprintfd( "IRQ 9 called\n" );
  beginIRQ(9);
  BDManager::getInstance()->serviceIRQ( 9 );
  endIRQ(9);
}

extern "C" void arch_irqHandler_11();
extern "C" void irqHandler_11()
{
  debug(A_INTERRUPTS, "Interrupt vector %u (%x) called by core %zx\n", 11, 11 + 0x20, ArchMulticore::getCpuID());
  kprintfd( "IRQ 11 called\n" );
  beginIRQ(11);
  BDManager::getInstance()->serviceIRQ( 11 );
  endIRQ(11);
}

extern "C" void arch_irqHandler_14();
extern "C" void irqHandler_14()
{
  debug(A_INTERRUPTS, "Interrupt vector %u (%u) called by core %zx\n", 14, 14 + 0x20, ArchMulticore::getCpuID());
  //kprintfd( "IRQ 14 called\n" );
  beginIRQ(14);
  BDManager::getInstance()->serviceIRQ( 14 );
  endIRQ(14);
}

extern "C" void arch_irqHandler_15();
extern "C" void irqHandler_15()
{
  debug(A_INTERRUPTS, "Interrupt vector %u (%x) called\n", 15, 15 + 0x20);
  //kprintfd( "IRQ 15 called\n" );
  beginIRQ(15);
  BDManager::getInstance()->serviceIRQ( 15 );
  endIRQ(15);
}

extern "C" void arch_irqHandler_90();
extern "C" void irqHandler_90()
{
        beginIRQ(90 - 0x20);
        debug(A_INTERRUPTS, "IRQ 90 called, cpu %zu halting\n", ArchMulticore::getCpuID());
        if (currentThread != 0)
        {
                debug(BACKTRACE, "CPU %zu backtrace:\n", ArchMulticore::getCpuID());
                currentThread->printBacktrace(false);
        }
        while(1)
                asm("hlt\n");
        endIRQ(90 - 0x20);
}

extern "C" void arch_irqHandler_99();
extern "C" void irqHandler_99()
{
        beginIRQ(99 - 0x20); // TODO: Fix APIC interrupt numbering
        debug(A_INTERRUPTS, "IRQ 99 called, performing TLB shootdown on CPU %zx\n", ArchMulticore::getCpuID());

        TLBShootdownRequest* shootdown_list = cpu_info.tlb_shootdown_list.exchange(nullptr);

        if(shootdown_list == nullptr)
        {
                debug(A_INTERRUPTS, "TLB shootdown for CPU %zx already handled previously\n", ArchMulticore::getCpuID());
        }

        while(shootdown_list != nullptr)
        {
                debug(A_INTERRUPTS, "CPU %zx performing TLB shootdown for request %zx, addr %zx from CPU %zx, target %zx\n", ArchMulticore::getCpuID(), shootdown_list->request_id, shootdown_list->addr, shootdown_list->orig_cpu, shootdown_list->target);
                assert(shootdown_list->target == ArchMulticore::getCpuID());
                assert(cpu_info.getCpuID() == ArchMulticore::getCpuID());
                assert(cpu_info.lapic.ID() == ArchMulticore::getCpuID());
                assert(cpu_info.lapic.readID() == ArchMulticore::getCpuID());
                ArchMemory::flushLocalTranslationCaches(shootdown_list->addr);

                TLBShootdownRequest* next = shootdown_list->next;
                // Object is invalid as soon as we acknowledge it
                //shootdown_list->ack++;
                assert((shootdown_list->ack & (1 << ArchMulticore::getCpuID())) == 0);
                assert(shootdown_list->orig_cpu != ArchMulticore::getCpuID());

                shootdown_list->ack |= (1 << ArchMulticore::getCpuID());
                assert(shootdown_list != next);
                shootdown_list = next;
        }

        endIRQ(99 - 0x20);
}

extern "C" void arch_irqHandler_100();
extern "C" void irqHandler_100()
{
        // No EOI here!
        debug(A_INTERRUPTS, "IRQ 100 called by CPU %zu, spurious APIC interrupt\n", ArchMulticore::getCpuID());
}

extern "C" void arch_syscallHandler();
extern "C" void syscallHandler()
{
  currentThread->switch_to_userspace_ = 0;
  currentThreadRegisters = currentThread->kernel_registers_;
  ArchInterrupts::enableInterrupts();

  currentThread->user_registers_->rax =
    Syscall::syscallException(currentThread->user_registers_->rax,
                  currentThread->user_registers_->rbx,
                  currentThread->user_registers_->rcx,
                  currentThread->user_registers_->rdx,
                  currentThread->user_registers_->rsi,
                  currentThread->user_registers_->rdi);

  ArchInterrupts::disableInterrupts();
  currentThread->switch_to_userspace_ = 1;
  currentThreadRegisters = currentThread->user_registers_;
  arch_contextSwitch();
  assert(false);
}


extern const char* errors[];
extern "C" void arch_errorHandler();
extern "C" void errorHandler(size_t num, size_t rip, size_t cs, size_t spurious)
{
  kprintfd("%zx\n",cs);
  if (spurious)
  {
    assert(num < 128 && "there are only 128 interrupts");
    debug(CPU_ERROR, "Spurious Interrupt %zu (%zx)\n", num, num);
  }
  else
  {
    assert(num < 32 && "there are only 32 CPU errors");
    debug(CPU_ERROR, "\033[1;31m%s\033[0;39m\n", errors[num]);
  }
  const bool userspace = (cs & 0x3);
  debug(CPU_ERROR, "Instruction Pointer: %zx, Userspace: %d - currentThread(): %p %zd" ":%s, switch_to_userspace_: %d\n",
        rip, userspace, currentThread,
        currentThread ? currentThread->getTID() : -1UL, currentThread ? currentThread->getName() : 0,
        currentThread ? currentThread->switch_to_userspace_ : -1);

  const Stabs2DebugInfo* deb = kernel_debug_info;
  assert(currentThread && "there should be no fault before there is a current thread");
  assert(currentThread->kernel_registers_ && "every thread needs kernel registers");
  if (userspace)
  {
    assert(currentThread->loader_ && "User Threads need to have a Loader");
    assert(currentThread->user_registers_ && (currentThread->user_registers_->cr3 == currentThread->kernel_registers_->cr3 &&
           "User and Kernel CR3 register values differ, this most likely is a bug!"));
    deb = currentThread->loader_->getDebugInfos();
  }
  if(deb && rip)
  {
    debug(CPU_ERROR, "This Fault was probably caused by:");
    deb->printCallInformation(rip);
  }
  ArchThreads::printThreadRegisters(currentThread, false);
  currentThread->printBacktrace(true);

  if (spurious)
  {
    if (currentThread->switch_to_userspace_)
      arch_contextSwitch();
  }
  else
  {
    currentThread->switch_to_userspace_ = false;
    currentThreadRegisters = currentThread->kernel_registers_;
    ArchInterrupts::enableInterrupts();
    debug(CPU_ERROR, "Terminating process...\n");
    currentThread->kill();
    assert(false);
  }
}

#include "ErrorHandlers.h" // error handler definitions and irq forwarding definitions
