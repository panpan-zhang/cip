#include "utils.h"
extern int debug;

int run_cmd(char *cmd, ...)
{
    va_list ap;
    char buf[CMDBUFLEN];
    va_start(ap, cmd);
    vsnprintf(buf, CMDBUFLEN, cmd, ap);

    va_end(ap);

    if (debug)
    {
        printf("EXEC: %s\n", buf);
    }

    return system(buf);
}

void print_err(char *format, ...)
{
    va_list ap;
    va_start(ap,format);

    vfprintf(stderr, format, ap);

    va_end(ap);
}

uint32_t sum_every_16bits(void *addr, int count)
{
    register uint32_t sum = 0;
    uint16_t *ptr = addr;

    while (count > 1) {
        sum += *ptr++;
        count -= 2;
    }

    if (count > 0)
        sum += *(uint8_t *) ptr;

    return sum;
}

uint16_t checksum(void *addr, int count, int start_sum)
{
    uint32_t sum = start_sum;

    sum += sum_every_16bits(addr, count);

    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}

uint32_t parse_ipv4_string(char *addr)
{
    uint8_t addr_byte[4];
    sscanf(addr, "%hhu.%hhu.%hhu.%hhu", &addr_byte[3], &addr_byte[2], &addr_byte[1],
                                        &addr_byte[0]);

    return addr_byte[0] | addr_byte[1] << 8 | addr_byte[2] << 16 | addr_byte[3] << 24;
}