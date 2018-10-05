#include "stdio.h"
#include "nonstd.h"

int main()
{
        int status;
        size_t value = 0;
        printf("getValue test\n");

        status = getValue(0x10, &value);
        if(!status)
        {
                printf("getValue, key: 0x10, value: %zx\n", value);
        }
        else
        {
                printf("getValue, key: 0x10 failed, status: %d\n", status);
        }

        status = getValue(0x11, &value);

        if(!status)
        {
                printf("getValue, key: 0x11, value: %zx\n", value);
        }
        else
        {
                printf("getValue, key: 0x11 failed, status: %d\n", status);
        }

        status = getValue(0x45, &value);
        if(!status)
        {
                printf("getValue, key: 0x45, value: %zx\n", value);
        }
        else
        {
                printf("getValue, key: 0x45 failed, status: %d\n", status);
        }

        status = getValue(0x50, &value);
        if(!status)
        {
                printf("getValue, key: 0x50, value: %zx\n", value);
        }
        else
        {
                printf("getValue, key: 0x50 failed, status: %d\n", status);
        }


        return 0;
}
