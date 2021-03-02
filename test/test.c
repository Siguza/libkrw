#include <stdio.h>
#include "libkrw.h"

int main(void)
{
    uint64_t base = 0;
    int r = kbase(&base);
    printf("base: %d, 0x%llx\n", r, base);
    if(r != 0) return r;

    uint32_t magic = 0;
    r = kread(base, &magic, sizeof(magic));
    printf("magic: %d, 0x%x\n", r, magic);
    if(r != 0) return r;

    uint64_t alloc = 0;
    r = kmalloc(&alloc, 0x10);
    printf("kmalloc: %d, 0x%llx\n", r, alloc);
    if(r != 0) return r;

    uint64_t data = 0x1122334455667788;
    r = kwrite(&data, alloc, 0x8);
    printf("kwrite: %d\n", r);
    if(r != 0) return r;

    uint64_t back = 0;
    r = kread(alloc + 0x4, &back, 8);
    printf("kread: %d, 0x%llx\n", r, back);
    if(r != 0) return r;

    r = kdealloc(alloc, 0x8);
    printf("kdealloc: %d\n", r);
    if(r != 0) return r;

    return 0;
}
