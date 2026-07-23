#ifndef MM_H
#define MM_H
void init_memory(void);
void* kmalloc(unsigned int size);
void kfree(void* ptr);
void* kcalloc(unsigned int num, unsigned int size);
void print_memory_stats(void);
#endif
