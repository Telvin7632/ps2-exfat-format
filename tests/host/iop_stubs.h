/* Test hooks exposed by iop_stubs.c */
#ifndef HOST_IOP_STUBS_H
#define HOST_IOP_STUBS_H

#include "irx_imports.h"

#define STUB_MAX_BD 20 /* MAX_CONNECTIONS in PS2SDK bdm.c */

extern struct block_device *stub_bd_list[STUB_MAX_BD];
extern int stub_live_allocs;
extern int stub_live_threads;
extern int stub_live_semas;
extern int stub_not_dormant_left;
extern int stub_delete_calls;

/* iop/main.c's _start, renamed at compile time (-D_start=exfatfmt_start) */
int exfatfmt_start(int argc, char *argv[]);

#endif
