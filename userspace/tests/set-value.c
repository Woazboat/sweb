#include "stdio.h"
#include "nonstd.h"

int main()
{
        int status;
        printf("setValue test\n");

        status = setValue(0x10, 0x123);
        printf("setValue(0x10, 0x123) status = %d\n", status);

        status = setValue(0x11, 0x456);
        printf("setValue(0x11, 0x456) status = %d\n", status);

        status = setValue(0x45, 0xDEADBEEF);
        printf("setValue(0x45, 0xDEADBEEF) status = %d\n", status);

        return 0;
}
