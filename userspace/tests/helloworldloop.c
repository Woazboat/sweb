#include "stdio.h"
#include "nonstd.h"
#include "assert.h"


int main(int argc, char *argv[])
{
        printf("Starting test loop\n");
        while(1)
        {
                createprocess("/usr/helloworld.sweb", 0);
        }
}