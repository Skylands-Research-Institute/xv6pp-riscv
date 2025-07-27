#pragma once

#include "types.h"

void argint(int n, int *ip);
int argstr(int n, char *buf, int max);
void argaddr(int n, uint64 *ip);
int fetchstr(uint64 addr, char *buf, int max);
int fetchaddr(uint64 addr, uint64 *ip);
