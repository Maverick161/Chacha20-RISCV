#ifndef CHACHA20_H
#define CHACHA20_H

typedef unsigned int  uint32_t;
typedef unsigned char uint8_t;

void chacha20_quarter_round(uint32_t *a, uint32_t *b,
                            uint32_t *c, uint32_t *d);

#endif