#include "ArchInterrupts.h"
#include "8259.h"
#include "ProgrammableIntervalTimer.h"
#include "ports.h"
#include "InterruptUtils.h"
#include "ArchThreads.h"
#include "assert.h"
#include "Thread.h"
#include "APIC.h"
#include "debug.h"
#include "ArchMemory.h"
#include "PageManager.h"
#include "ArchMulticore.h"
#include "Scheduler.h"

void ArchInterrupts::initialise()
{
  disableInterrupts();
  InterruptUtils::initialise();

  if(LocalAPIC::exists)
  {
          ArchMulticore::getCLS()->apic.mapAt(APIC_VADDR);
          ArchMulticore::getCLS()->apic.init();
  }

  if(IOAPIC::exists)
  {
          IO_APIC.mapAt(IOAPIC_VADDR);
          IO_APIC.init();
  }


  PIC8259::initialise8259s();
}

void ArchInterrupts::enableTimer()
{
  if(ArchMulticore::getCLS()->apic.isInitialized() && ArchMulticore::getCLS()->apic.usingAPICTimer())
  {
    ArchMulticore::getCLS()->apic.reg_vaddr_->lvt_timer.setMask(false);
  }
  else
  {
    if(IOAPIC::initialized)
    {
      IO_APIC.setIRQMask(2, false); // IRQ source override in APIC: 0 -> 2 // TODO: Shouldn't be hardcoded
    }
    else
    {
      PIC8259::enableIRQ(0);
    }
  }
}

void ArchInterrupts::setTimerFrequency(uint32 freq) {
  debug(A_INTERRUPTS, "Set timer frequency %u\n", freq);
  uint16_t divisor;
  if(freq < (uint32)(1193180. / (1 << 16) + 1)) {
    divisor = 0;
  } else {
    divisor = (uint16)(1193180 / freq);
  }

  PIT::PITCommandRegister command{};
  command.bcd_mode = 0;
  command.operating_mode = 3; // square wave generator
  command.access_mode = 3; // send low + high byte of reload value/divisor
  command.channel = 0;

  PIT::init(command.value, divisor);
}

void ArchInterrupts::disableTimer()
{
  if(ArchMulticore::getCLS()->apic.isInitialized() && ArchMulticore::getCLS()->apic.usingAPICTimer())
  {
    ArchMulticore::getCLS()->apic.reg_vaddr_->lvt_timer.setMask(true);
  }
  else
  {
    if(IOAPIC::initialized)
    {
      IO_APIC.setIRQMask(2, true); // IRQ source override in APIC: 0 -> 2
    }
    else
    {
      PIC8259::disableIRQ(0);
    }
  }
}

void ArchInterrupts::enableKBD()
{
        enableIRQ(1);
        enableIRQ(9);
}

void ArchInterrupts::disableKBD()
{
        disableIRQ(1);
}

void ArchInterrupts::enableIRQ(uint16 num)
{
        if(IOAPIC::initialized)
        {
                IO_APIC.setIRQMask(num, false); // TODO: handle irq source overrides
        }
        else
        {
                PIC8259::enableIRQ(num);
        }
}

void ArchInterrupts::disableIRQ(uint16 num)
{
        if(IOAPIC::initialized)
        {
                IO_APIC.setIRQMask(num, true); // TODO: handle irq source overrides
        }
        else
        {
                PIC8259::disableIRQ(num);
        }
}

void ArchInterrupts::EndOfInterrupt(uint16 number)
{
  if((LocalAPIC::exists && ArchMulticore::getCLS()->apic.isInitialized()) &&
     (IOAPIC::initialized ||
      ((number == 0) && ArchMulticore::getCLS()->apic.usingAPICTimer())))
  {
          ArchMulticore::getCLS()->apic.sendEOI(number + 0x20);
  }
  else
  {
          PIC8259::sendEOI(number);
  }
}

void ArchInterrupts::enableInterrupts()
{
   asm("sti");
}

bool ArchInterrupts::disableInterrupts()
{
  uint64 ret_val;
  asm("pushfq\n"
      "popq %0\n"
      "cli"
      : "=a"(ret_val));
  return (ret_val & (1 << 9));  //testing IF Flag
}

bool ArchInterrupts::testIFSet()
{
  uint64 ret_val;

  asm("pushfq\n"
      "popq %0\n"
      : "=a"(ret_val));
  return (ret_val & (1 << 9));  //testing IF Flag
}

void ArchInterrupts::yieldIfIFSet()
{
  if (system_state == RUNNING && currentThread() && testIFSet())
  {
    ArchThreads::yield();
  }
  else
  {
    asm("nop");
  }
}


struct context_switch_registers {
  uint64 ds;
  uint64 es;
  uint64 r15;
  uint64 r14;
  uint64 r13;
  uint64 r12;
  uint64 r11;
  uint64 r10;
  uint64 r9;
  uint64 r8;
  uint64 rdi;
  uint64 rsi;
  uint64 rbp;
  uint64 rbx;
  uint64 rdx;
  uint64 rcx;
  uint64 rax;
  uint64 rsp;
};

struct interrupt_registers {
  uint64 rip;
  uint64 cs;
  uint64 rflags;
  uint64 rsp;
  uint64 ss;
};

#include "kprintf.h"

extern "C" void arch_saveThreadRegisters(uint64* base, uint64 error)
{
  register struct context_switch_registers* registers;
  registers = (struct context_switch_registers*) base;
  register struct interrupt_registers* iregisters;
  iregisters = (struct interrupt_registers*) (base + sizeof(struct context_switch_registers)/sizeof(uint64) + error);
  assert(ArchMulticore::CLSinitialized());
  register ArchThreadRegisters* info = ArchMulticore::getCLS()->scheduler.getCurrentThreadRegisters();
  asm("fnsave %[fpu]\n"
      "frstor %[fpu]\n"
      :
      : [fpu]"m"((info->fpu))
      :);
  info->rsp = iregisters->rsp;
  info->rip = iregisters->rip;
  info->cs = iregisters->cs;
  info->rflags = iregisters->rflags;
  info->es = registers->es;
  info->ds = registers->ds;
  info->r15 = registers->r15;
  info->r14 = registers->r14;
  info->r13 = registers->r13;
  info->r12 = registers->r12;
  info->r11 = registers->r11;
  info->r10 = registers->r10;
  info->r9 = registers->r9;
  info->r8 = registers->r8;
  info->rdi = registers->rdi;
  info->rsi = registers->rsi;
  info->rbx = registers->rbx;
  info->rdx = registers->rdx;
  info->rcx = registers->rcx;
  info->rax = registers->rax;
  info->rbp = registers->rbp;
  assert(!currentThread() || currentThread()->isStackCanaryOK());
}

extern "C" void arch_contextSwitch()
{
  debug(A_INTERRUPTS, "CPU %zx, context switch to thread %p = %s\n", ArchMulticore::getCpuID(), currentThread(), currentThread()->getName());

  if((ArchMulticore::getCpuID() == 0) && PIC8259::outstanding_EOIs_) // TODO: Check local APIc for outstanding interrupts
  {
    debug(A_INTERRUPTS, "%zu outstanding End-Of-Interrupt signal(s) on context switch. Probably called yield in the wrong place (e.g. in the scheduler)\n", PIC8259::outstanding_EOIs_);
    assert(!((ArchMulticore::getCpuID() == 0) && PIC8259::outstanding_EOIs_));
  }
  if (currentThread()->switch_to_userspace_)
  {
    assert(currentThread()->holding_lock_list_ == 0 && "Never switch to userspace when holding a lock! Never!");
    assert(currentThread()->lock_waiting_on_ == 0 && "How did you even manage to execute code while waiting for a lock?");
  }
  assert(currentThread()->isStackCanaryOK() && "Kernel stack corruption detected.");
  ArchThreadRegisters info = *ArchMulticore::getCLS()->scheduler.getCurrentThreadRegisters();
  assert(info.rsp0 >= USER_BREAK);
  ArchMulticore::getCLS()->tss.rsp0 = info.rsp0;
  asm("frstor %[fpu]\n" : : [fpu]"m"(info.fpu));
  asm("mov %[cr3], %%cr3\n" : : [cr3]"r"(info.cr3));
  asm("push %[ss]" : : [ss]"m"(info.ss));
  asm("push %[rsp]" : : [rsp]"m"(info.rsp));
  asm("push %[rflags]\n" : : [rflags]"m"(info.rflags));
  asm("push %[cs]\n" : : [cs]"m"(info.cs));
  asm("push %[rip]\n" : : [rip]"m"(info.rip));
  asm("mov %[rsi], %%rsi\n" : : [rsi]"m"(info.rsi));
  asm("mov %[rdi], %%rdi\n" : : [rdi]"m"(info.rdi));
  asm("mov %[es], %%es\n" : : [es]"m"(info.es));
  asm("mov %[ds], %%ds\n" : : [ds]"m"(info.ds));
  asm("mov %[r8], %%r8\n" : : [r8]"m"(info.r8));
  asm("mov %[r9], %%r9\n" : : [r9]"m"(info.r9));
  asm("mov %[r10], %%r10\n" : : [r10]"m"(info.r10));
  asm("mov %[r11], %%r11\n" : : [r11]"m"(info.r11));
  asm("mov %[r12], %%r12\n" : : [r12]"m"(info.r12));
  asm("mov %[r13], %%r13\n" : : [r13]"m"(info.r13));
  asm("mov %[r14], %%r14\n" : : [r14]"m"(info.r14));
  asm("mov %[r15], %%r15\n" : : [r15]"m"(info.r15));
  asm("mov %[rdx], %%rdx\n" : : [rdx]"m"(info.rdx));
  asm("mov %[rcx], %%rcx\n" : : [rcx]"m"(info.rcx));
  asm("mov %[rbx], %%rbx\n" : : [rbx]"m"(info.rbx));
  asm("mov %[rax], %%rax\n" : : [rax]"m"(info.rax));
  asm("mov %[rbp], %%rbp\n" : : [rbp]"m"(info.rbp));
  asm("testl $3, 8(%rsp)\n"
      "jz 1f\n"
      "swapgs\n"
      "1: iretq\n");
  assert(false);
}
