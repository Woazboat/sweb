#pragma once

#include "types.h"
#include "ACPI.h"

#define APIC_VADDR   0xffffffff81200000
#define IOAPIC_VADDR 0xffffffff81201000

struct LocalAPIC_InterruptCommandRegisterLow
{
        volatile uint32 vector                : 8;  // 0-7
        volatile uint32 delivery_mode         : 3;  // 8-10
        volatile uint32 destination_mode      : 1;  // 11
        volatile uint32 delivery_status       : 1;  // 12
        volatile uint32 reserved              : 1;  // 13
        volatile uint32 level                 : 1;  // 14
        volatile uint32 trigger_mode          : 1;  // 15
        volatile uint32 reserved2             : 2;  // 16-17
        volatile uint32 destination_shorthand : 2;  // 18-19
        volatile uint32 reserved3             : 12; // 20-31
} __attribute__ ((packed));

struct LocalAPIC_InterruptCommandRegisterHigh
{
        volatile uint32 reserved    : 24; //  0-23
        volatile uint32 destination : 8;  // 24-31
} __attribute__ ((packed));

struct LocalAPIC_SpuriousInterruptVector
{
        volatile uint32 vector         : 8;
        volatile uint32 enable         : 1;
        volatile uint32 focus_checking : 1;
        volatile uint32 reserved       : 22;

        void setSpuriousInterruptNumber(uint8 num) volatile;
} __attribute__ ((packed));

struct LocalAPIC_IDRegister
{
        volatile uint32 reserved : 24;
        volatile uint32 id       : 8;
} __attribute__ ((packed));

struct LocalAPIC_VersionRegister
{
        volatile uint32 version                              : 8;
        volatile uint32 reserved1                            : 8;
        volatile uint32 max_lvt_entry                        : 8;
        volatile uint32 eoi_broadcast_suppression_supported  : 1;
        volatile uint32 reserved2                            : 7;
} __attribute__ ((packed));

struct LocalAPIC_LVT_TimerRegister
{
        volatile uint32 vector          :  8;
        volatile uint32 reserved1       :  4;
        volatile uint32 delivery_status :  1;
        volatile uint32 reserved2       :  3;
        volatile uint32 mask            :  1;
        volatile uint32 timer_mode      :  2; // 0: one-shot, 1: periodic, 3: TSC-Deadline
        volatile uint32 reserved3       : 13;

        void setVector(uint8) volatile;
        void setMode(uint8) volatile;
        void setMask(bool mask) volatile;
} __attribute__ ((packed));

struct LocalAPIC_LVT_LINTRegister
{
        volatile uint32 vector          :  8;
        volatile uint32 delivery_mode   :  3;
        volatile uint32 reserved1       :  1;
        volatile uint32 delivery_status :  1;
        volatile uint32 pin_polarity    :  1;
        volatile uint32 remote_irr      :  1;
        volatile uint32 trigger_mode    :  1;
        volatile uint32 mask            :  1;
        volatile uint32 reserved3       : 15;
} __attribute__ ((packed));

struct LocalAPIC_LVT_ErrorRegister
{
        volatile uint32 vector          :  8;
        volatile uint32 reserved1       :  4;
        volatile uint32 delivery_status :  1;
        volatile uint32 reserved2       :  3;
        volatile uint32 mask            :  1;
        volatile uint32 reserved3       : 15;
} __attribute__ ((packed));

struct LocalAPIC_ErrorStatusRegister
{
        volatile uint32 illegal_register_access : 1;
        volatile uint32 recv_illegal_vector     : 1;
        volatile uint32 send_illegal_vector     : 1;
        volatile uint32 redirectable_ipi        : 1;
        volatile uint32 recv_accept_error       : 1;
        volatile uint32 send_accept_error       : 1;
        volatile uint32 recv_checksum_error     : 1;
        volatile uint32 send_checksum_error     : 1;
        volatile uint32 reserved                : 24;
} __attribute__ ((packed));

struct LocalAPIC_TimerDivideConfigRegister
{
        volatile uint32 divisor_l : 2;
        volatile uint32 reserved1 : 1;
        volatile uint32 divisor_h : 1;
        volatile uint32 reserved2 : 28;

        void setTimerDivisor(uint8 divisor) volatile;
} __attribute__ ((packed));

struct LocalAPIC_PriorityRegister
{
        volatile uint32 priority_sub_class : 4;
        volatile uint32 priority_class     : 4;
        volatile uint32 reserved           : 24;
} __attribute__ ((packed));


struct LocalAPICRegisters
{
        volatile char reserved1[0x10*2]; // 0-1

        volatile LocalAPIC_IDRegister local_apic_id; // 2
        volatile char padding1[12];

        volatile const LocalAPIC_VersionRegister local_apic_version; // 3
        volatile char padding2[12];

        volatile char reserved2[0x10*4]; // 4-7

        volatile LocalAPIC_PriorityRegister task_priority; // 8
        volatile char padding3[12];

        volatile LocalAPIC_PriorityRegister arbitration_priority; // 9
        volatile char padding4[12];

        volatile LocalAPIC_PriorityRegister processor_priority; // 10
        volatile char padding5[12];

        volatile uint32 eoi; // 11
        volatile char padding6[12];

        volatile char reserved[0x10]; // 12

        volatile uint32 logical_destination; // 13
        volatile char padding7[12];

        volatile uint32 destination_format; // 14
        volatile char padding8[12];

        volatile LocalAPIC_SpuriousInterruptVector s_int_vect; // 15
        volatile char padding9[12];

        struct
        {
                volatile uint32 isr;
                volatile char padding[12];
        }ISR[8] __attribute__((packed)); //

        struct
        {
                volatile uint32 tmr;
                volatile char padding[12];
        }TMR[8] __attribute__((packed));

        struct
        {
                volatile uint32 irr;
                volatile char padding[12];
        }IRR[8] __attribute__((packed));

        volatile LocalAPIC_ErrorStatusRegister error_status;
        volatile char padding10[12];

        volatile char reserved3[0x10*6];

        volatile uint32 lvt_cmci;
        volatile char padding11[12];

        volatile LocalAPIC_InterruptCommandRegisterLow ICR_low;
        volatile char padding12[12];

        volatile LocalAPIC_InterruptCommandRegisterHigh ICR_high;
        volatile char padding13[12];

        volatile LocalAPIC_LVT_TimerRegister lvt_timer;
        volatile char padding14[12];

        volatile uint32 lvt_thermal_sensor;
        volatile char padding15[12];

        volatile uint32 lvt_performance_counter;
        volatile char padding16[12];

        volatile LocalAPIC_LVT_LINTRegister lvt_lint0;
        volatile char padding17[12];

        volatile LocalAPIC_LVT_LINTRegister lvt_lint1;
        volatile char padding18[12];

        volatile LocalAPIC_LVT_ErrorRegister lvt_error;
        volatile char padding19[12];

        volatile uint32 init_timer_count;
        volatile char padding20[12];

        volatile uint32 current_timer_count;
        volatile char padding21[12];

        volatile char reserved5[0x10*4];

        volatile LocalAPIC_TimerDivideConfigRegister timer_divide_config;
        volatile char padding22[12];

        volatile char reserved6[0x10];
} __attribute__((packed));

static_assert(sizeof(LocalAPICRegisters) == 0x400);


class LocalAPIC
{
public:
        LocalAPICRegisters registers;
        void sendEOI(size_t num);
        void mapAt(size_t addr);
        void enable(bool = true);
        void setTimerPeriod(uint32 count) volatile;

        bool checkIRR(uint8 num) volatile;
        bool checkISR(uint8 num) volatile;
private:
        LocalAPIC() = delete;
        LocalAPIC(const LocalAPIC&) = delete;
        LocalAPIC& operator=(const LocalAPIC&) = delete;
};

class IOAPIC
{
public:
        struct IOAPIC_r0
        {
                volatile uint32 reserved1  : 24; //  0-23
                volatile uint32 io_apic_id :  4; // 24-27
                volatile uint32 reserved2  :  4; // 28-31
        };

        struct IOAPIC_r1
        {
                const volatile uint32 version   : 8; //  0- 7
                const volatile uint32 reserved1 : 8; //  8-15
                const volatile uint32 max_redir : 8; // 16-23
                const volatile uint32 reserved2 : 8; // 24-31
        };

        struct IOAPIC_r2
        {
                volatile uint32 reserved1            : 24; // 0-23
                volatile uint32 arbitration_priority : 4;  // 24-27
                volatile uint32 reserved2            : 4;  // 27-31
        };

        struct IOAPIC_redir_entry
        {
                volatile uint32 interrupt_vector : 8; // Allowed values: 0x10-0xFE
                volatile uint32 delivery_mode    : 3;
                volatile uint32 destination_mode : 1;
          const volatile uint32 pending_busy     : 1;
                volatile uint32 polarity         : 1;
                volatile uint32 lvl_trig_recvd   : 1;
                volatile uint32 trigger_mode     : 1;
                volatile uint32 interrupt_mask   : 1;
                volatile uint32 reserved1        : 15;

                volatile uint32 reserved2        : 24;
                volatile uint32 destination      : 8;
        };

        struct IOAPICRegisters
        {
                volatile IOAPIC_r0 r0;
                volatile IOAPIC_r1 r1;
                volatile IOAPIC_r2 r2;
                volatile IOAPIC_redir_entry redir[0x18];
        };

        static bool initialized;
        static IOAPICRegisters* phys_addr;

        explicit IOAPIC();
        IOAPIC(IOAPICRegisters* regs);

        void mapAt(void* addr);
private:
        IOAPICRegisters* reg_;
};


LocalAPIC* initAPIC(ACPI_MADTHeader* madt);

extern LocalAPIC* local_APIC;
extern IOAPIC IO_APIC;
