/* Simple test to verify we can access vm86 on the target system */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

/* vm86 syscall number on x86 */
#define __NR_vm86 113

int main()
{
    printf("Testing vm86 availability...\n");
    printf("Architecture: ");
    
#ifdef __i386__
    printf("i386 (32-bit x86)\n");
    
    /* Try to make the syscall to see if it's available */
    long result = syscall(__NR_vm86, 0, 0);
    if (result == -1) {
        perror("vm86 syscall");
        printf("vm86 is not available or not permitted\n");
        return 1;
    } else {
        printf("vm86 syscall appears to be available\n");
        return 0;
    }
    
#elif defined(__x86_64__)
    printf("x86_64 (64-bit)\n");
    printf("vm86 is not available on 64-bit systems\n");
    return 1;
    
#else
    printf("Unknown/unsupported architecture\n");
    return 1;
#endif
}