#include <stdint.h>
#include <stdio.h>

int
main(void)
{
    uint64_t sum = 0;
    uint8_t step = sizeof(uint64_t);
    //for (uint64_t i = 0x10000; i < 0x7c00000; i += step) {
    //for (uint64_t i = 0x7c00000; i > 0x10000; i -= step) {
    for (uint64_t i = 0x1b000; i > 0x10000; i -= step) {
        uint64_t value = *((uint64_t*)i);
        printf("%016lx\n", value);
        sum += value;
    }

    printf("memory sum is %ld\n", sum);

    return 0;
}
