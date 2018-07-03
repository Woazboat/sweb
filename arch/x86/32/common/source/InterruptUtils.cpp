#include "InterruptUtils.h"

#include "ArchSerialInfo.h"
#include "BDManager.h"
#include "ArchMemory.h"
#include "ArchThreads.h"
#include "ArchCommon.h"
#include "kprintf.h"
#include "Scheduler.h"

#include "SerialManager.h"
#include "KeyboardManager.h"
#include "ArchInterrupts.h"
#include "backtrace.h"

#include "Thread.h"
#include "Loader.h"
#include "Syscall.h"
#include "paging-definitions.h"
#include "offsets.h"
#include "PageFaultHandler.h"
#include "Stabs2DebugInfo.h"

#include "ErrorHandlers.h" // error handler definitions and irq forwarding definitions

#define LO_WORD(x) (((uint32)(x)) & 0x0000FFFF)
#define HI_WORD(x) ((((uint32)(x)) >> 16) & 0x0000FFFF)

#define GATE_SIZE_16_BIT     0 // use 16- bit push
#define GATE_SIZE_32_BIT     1 // use 32- bit push

#define TYPE_TRAP_GATE       7 // trap gate, i.e. IF flag is *not* cleared
#define TYPE_INTERRUPT_GATE  6 // interrupt gate, i.e. IF flag *is* cleared


struct GateDesc
{
  uint16 offset_low;       // low word of handler entry point's address
  uint16 segment_selector; // (code) segment the handler resides in
  uint8 reserved  : 5;     // reserved. set to zero
  uint8 zeros     : 3;     // set to zero
  uint8 type      : 3;     // set to TYPE_TRAP_GATE or TYPE_INTERRUPT_GATE
  uint8 gate_size : 1;     // set to GATE_SIZE_16_BIT or GATE_SIZE_32_BIT
  uint8 unused    : 1;     // unsued - set to zero
  uint8 dpl       : 2;     // descriptor protection level
  uint8 present   : 1;     // present- flag - set to 1
  uint16 offset_high;      // high word of handler entry point's address
}__attribute__((__packed__));

extern "C" void arch_dummyHandler();
extern "C" void arch_dummyHandlerMiddle();

void InterruptUtils::initialise()
{
  size_t num_int_handlers = sizeof(handlers)/sizeof(handlers[0]);
  size_t max_int_num = 0;
  for(size_t i = 0; i < num_int_handlers; ++i)
  {
          debug(A_INTERRUPTS, "Interrupt handler, num: %4x, offset: %p\n", handlers[i].number, handlers[i].offset);
          max_int_num = Max(max_int_num, handlers[i].number);
  }
  size_t num_idt_entries = max_int_num + 1;

  // allocate memory for the IDT
  GateDesc *idt = new GateDesc[num_idt_entries];

  size_t dummy_handler_sled_size = (((size_t) arch_dummyHandlerMiddle) - (size_t) arch_dummyHandler);
  assert((dummy_handler_sled_size % 128) == 0 && "cannot handle weird padding in the kernel binary");
  dummy_handler_sled_size /= 128;


  for(size_t i = 0; i < num_idt_entries; ++i)
  {
    idt[i].offset_low       = LO_WORD(((size_t)arch_dummyHandler) + i*dummy_handler_sled_size);
    idt[i].offset_high      = HI_WORD(((size_t)arch_dummyHandler) + i*dummy_handler_sled_size);
    idt[i].gate_size        = GATE_SIZE_32_BIT;
    idt[i].present          = 1;
    idt[i].reserved         = 0;
    idt[i].segment_selector = KERNEL_CS;
    idt[i].type             = TYPE_INTERRUPT_GATE;
    idt[i].unused           = 0;
    idt[i].zeros            = 0;
    idt[i].dpl              = DPL_KERNEL_SPACE;
  }

  for(size_t i = 0; i < num_int_handlers; ++i)
  {
    uint32 handler_num = handlers[i].number;
    idt[handler_num].offset_low  = LO_WORD((size_t)handlers[i].offset);
    idt[handler_num].offset_high = HI_WORD((size_t)handlers[i].offset);
    idt[handler_num].dpl         = (handler_num == SYSCALL_INTERRUPT ? DPL_USER_SPACE : DPL_KERNEL_SPACE);
  }

  for(size_t i = 0; i < num_idt_entries; ++i)
  {
    debug(A_INTERRUPTS,
          "%4zx -- offset = %zx, offset_low = %x, offset_high = %x, present = %u, segment_selector = %x, type = %x, dpl = %x\n",
          i,
          (size_t)idt[i].offset_low | ((size_t)idt[i].offset_high << 16),
          idt[i].offset_low, idt[i].offset_high,
          idt[i].present, idt[i].segment_selector,
          idt[i].type, idt[i].dpl);
  }

  IDTR idtr;
  idtr.base = (uint32) idt;
  idtr.limit = sizeof(GateDesc) * num_idt_entries;
  lidt(&idtr);
}

void InterruptUtils::lidt(IDTR *idtr)
{
  asm volatile("lidt (%0) ": :"q" (idtr));
}

extern "C" void arch_irqHandler_0();
extern "C" void arch_contextSwitch();
extern "C" void irqHandler_0()
{
  ArchCommon::drawHeartBeat();

  Scheduler::instance()->incTicks();

  Scheduler::instance()->schedule();
  // kprintfd("irq0: Going to leave irq Handler 0\n");
  ArchInterrupts::EndOfInterrupt(0);
  arch_contextSwitch();
}

extern "C" void arch_irqHandler_65();
extern "C" void irqHandler_65()
{
  Scheduler::instance()->schedule();
  // kprintfd("irq65: Going to leave int Handler 65 to user\n");
  arch_contextSwitch();
}

extern "C" void arch_pageFaultHandler();
extern "C" void pageFaultHandler(uint32 address, uint32 error)
{
  PageFaultHandler::enterPageFault(address, error & FLAG_PF_USER,
                                   error & FLAG_PF_PRESENT,
                                   error & FLAG_PF_RDWR,
                                   error & FLAG_PF_INSTR_FETCH);
  if (currentThread->switch_to_userspace_)
    arch_contextSwitch();
  else
    asm volatile ("movl %%cr3, %%eax; movl %%eax, %%cr3;" ::: "%eax");
}

extern "C" void arch_irqHandler_1();
extern "C" void irqHandler_1()
{
  KeyboardManager::instance()->serviceIRQ();
  ArchInterrupts::EndOfInterrupt(1);
}

extern "C" void arch_irqHandler_3();
extern "C" void irqHandler_3()
{
  kprintfd("IRQ 3 called\n");
  SerialManager::getInstance()->service_irq(3);
  ArchInterrupts::EndOfInterrupt(3);
  kprintfd("IRQ 3 ended\n");
}

extern "C" void arch_irqHandler_4();
extern "C" void irqHandler_4()
{
  kprintfd("IRQ 4 called\n");
  SerialManager::getInstance()->service_irq(4);
  ArchInterrupts::EndOfInterrupt(4);
  kprintfd("IRQ 4 ended\n");
}

extern "C" void arch_irqHandler_6();
extern "C" void irqHandler_6()
{
  kprintfd("IRQ 6 called\n");
  kprintfd("IRQ 6 ended\n");
}

extern "C" void arch_irqHandler_9();
extern "C" void irqHandler_9()
{
  kprintfd("IRQ 9 called\n");
  BDManager::getInstance()->serviceIRQ(9);
  ArchInterrupts::EndOfInterrupt(9);
}

extern "C" void arch_irqHandler_11();
extern "C" void irqHandler_11()
{
  kprintfd("IRQ 11 called\n");
  BDManager::getInstance()->serviceIRQ(11);
  ArchInterrupts::EndOfInterrupt(11);
}

extern "C" void arch_irqHandler_14();
extern "C" void irqHandler_14()
{
  //kprintfd( "IRQ 14 called\n" );
  BDManager::getInstance()->serviceIRQ(14);
  ArchInterrupts::EndOfInterrupt(14);
}

extern "C" void arch_irqHandler_15();
extern "C" void irqHandler_15()
{
  //kprintfd( "IRQ 15 called\n" );
  BDManager::getInstance()->serviceIRQ(15);
  ArchInterrupts::EndOfInterrupt(15);
}

extern "C" void arch_syscallHandler();
extern "C" void syscallHandler()
{
  currentThread->switch_to_userspace_ = 0;
  currentThreadRegisters = currentThread->kernel_registers_;
  ArchInterrupts::enableInterrupts();

  currentThread->user_registers_->eax = Syscall::syscallException(currentThread->user_registers_->eax,
                                                                         currentThread->user_registers_->ebx,
                                                                         currentThread->user_registers_->ecx,
                                                                         currentThread->user_registers_->edx,
                                                                         currentThread->user_registers_->esi,
                                                                         currentThread->user_registers_->edi);

  ArchInterrupts::disableInterrupts();
  currentThread->switch_to_userspace_ = 1;
  currentThreadRegisters = currentThread->user_registers_;
  //ArchThreads::printThreadRegisters(currentThread,false);
  arch_contextSwitch();
}

extern Stabs2DebugInfo const *kernel_debug_info;
extern const char* errors[];
extern "C" void arch_errorHandler();
extern "C" void errorHandler(size_t num, size_t rip, size_t cs, size_t spurious)
{
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
  debug(CPU_ERROR, "Instruction Pointer: %zx, Userspace: %d - currentThread: %p %zd" ":%s, switch_to_userspace_: %d\n",
        rip, userspace, currentThread,
        currentThread ? currentThread->getTID() : -1, currentThread ? currentThread->getName() : 0,
        currentThread ? currentThread->switch_to_userspace_ : -1);

  const Stabs2DebugInfo* deb = kernel_debug_info;
  assert(currentThread && "there should be no fault before there is a current thread");
  assert(currentThread->kernel_registers_ && "every thread needs kernel registers");
  ArchThreadRegisters* registers_ = currentThread->kernel_registers_;
  if (userspace)
  {
    assert(currentThread->loader_ && "User Threads need to have a Loader");
    assert(currentThread->user_registers_ && (currentThread->user_registers_->cr3 == currentThread->kernel_registers_->cr3 &&
           "User and Kernel CR3 register values differ, this most likely is a bug!"));
    deb = currentThread->loader_->getDebugInfos();
    registers_ = currentThread->user_registers_;
  }
  if(deb && registers_->eip)
  {
    debug(CPU_ERROR, "This Fault was probably caused by:");
    deb->printCallInformation(registers_->eip);
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
  }
}
