#pragma once

#include "types.h"

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

typedef struct {
  uint32  number;      // handler number
  void (*offset)();    // pointer to handler function
}  __attribute__((__packed__)) InterruptHandlers;


typedef struct {
    uint16 limit;
    size_t base;
} __attribute__((__packed__)) IDTR ;


class InterruptUtils
{
public:
  static void initialise();

  static void lidt(IDTR *idtr);

  static void countPageFault(uint64 address);

private:
  static InterruptHandlers handlers[];
  static uint64 pf_address;
  static uint64 pf_address_counter;
};

