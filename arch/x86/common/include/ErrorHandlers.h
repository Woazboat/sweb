#ifdef _DUMMY_HANDLERS_H_
#error This file may not be included more than once
#else
#define _ERROR_HANDLERS_H_




const char* errors[32] = {
  "#DE: Divide by Zero",
  "",
  "",
  "",
  "#OF: Overflow (INTO Instruction)",
  "#BR: Bound Range Exceeded",
  "#OP: Invalid OP Code",
  "#NM: FPU Not Avaiable or Ready",
  "#DF: Double Fault",
  "#MF: FPU Segment Overrun",
  "#TS: Invalid Task State Segment (TSS)",
  "#NP: Segment Not Present (WTF ?)",
  "#SS: Stack Segment Fault",
  "#GF: General Protection Fault",
  "",
  "",
  "#MF: Floting Point Error",
  "#AC: Alignment Error (Unaligned Memory Reference)",
  "#MC: Machine Check Error",
  "#XF: SIMD Floting Point Error"
};

#define ERROR_HANDLER_DECL(x) extern "C" void arch_errorHandler_##x();
#define IRQ_HANDLER_DECL(x) extern "C" void arch_irqHandler_##x();

ERROR_HANDLER_DECL(0)
ERROR_HANDLER_DECL(4)
ERROR_HANDLER_DECL(5)
ERROR_HANDLER_DECL(6)
ERROR_HANDLER_DECL(7)
ERROR_HANDLER_DECL(8)
ERROR_HANDLER_DECL(9)
ERROR_HANDLER_DECL(10)
ERROR_HANDLER_DECL(11)
ERROR_HANDLER_DECL(12)
ERROR_HANDLER_DECL(13)
ERROR_HANDLER_DECL(16)
ERROR_HANDLER_DECL(17)
ERROR_HANDLER_DECL(18)
ERROR_HANDLER_DECL(19)
extern "C" void arch_pageFaultHandler();

IRQ_HANDLER_DECL(0)
IRQ_HANDLER_DECL(1)
IRQ_HANDLER_DECL(3)
IRQ_HANDLER_DECL(4)
IRQ_HANDLER_DECL(6)
IRQ_HANDLER_DECL(9)
IRQ_HANDLER_DECL(11)
IRQ_HANDLER_DECL(14)
IRQ_HANDLER_DECL(15)
IRQ_HANDLER_DECL(65)
extern "C" void arch_syscallHandler();

extern ArchThreadRegisters *currentThreadRegisters;
extern Thread *currentThread;

#define ERRORHANDLER(X) {X, &arch_errorHandler_##X},
#define IRQHANDLER(X) {X + 32, &arch_irqHandler_##X},
InterruptHandlers InterruptUtils::handlers[] = {
  ERRORHANDLER(0)
  ERRORHANDLER(4)
  ERRORHANDLER(5)
  ERRORHANDLER(6)
  ERRORHANDLER(7)
  ERRORHANDLER(8)
  ERRORHANDLER(9)
  ERRORHANDLER(10)
  ERRORHANDLER(11)
  ERRORHANDLER(12)
  ERRORHANDLER(13)
  {14, &arch_pageFaultHandler},
  ERRORHANDLER(16)
  ERRORHANDLER(17)
  ERRORHANDLER(18)
  ERRORHANDLER(19)
  IRQHANDLER(0)
  IRQHANDLER(1)
  IRQHANDLER(3)
  IRQHANDLER(4)
  IRQHANDLER(6)
  IRQHANDLER(9)
  IRQHANDLER(11)
  IRQHANDLER(14)
  IRQHANDLER(15)
  {65, &arch_irqHandler_65},
  {128, &arch_syscallHandler}
};

#endif
