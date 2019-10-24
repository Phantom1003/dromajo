//******************************************************************************
// Copyright (C) 2018,2019, Esperanto Technologies Inc.
// The copyright to the computer program(s) herein is the
// property of Esperanto Technologies, Inc. All Rights Reserved.
// The program(s) may be used and/or copied only with
// the written permission of Esperanto Technologies and
// in accordance with the terms and conditions stipulated in the
// agreement/contract under which the program(s) have been supplied.
//------------------------------------------------------------------------------

#include "dromajo.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/stat.h>
#include <signal.h>

#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "riscv_machine.h"
#include "LiveCacheCore.h"

//#define REGRESS_COSIM 1
#ifdef REGRESS_COSIM
#include "dromajo_cosim.h"
#endif


int iterate_core(RISCVMachine *m, int hartid)
{
    if (m->common.maxinsns-- <= 0)
        /* Succeed after N instructions without failure. */
        return 0;

    RISCVCPUState *cpu = m->cpu_state[hartid];

    /* Instruction that raises exceptions should be marked as such in
     * the trace of retired instructions.
     */
    uint64_t last_pc    = virt_machine_get_pc(m, hartid);
    int      priv       = riscv_get_priv_level(cpu);
    uint32_t insn_raw   = -1;
    (void) riscv_read_insn(cpu, &insn_raw, last_pc);
    int      keep_going = virt_machine_run(m, hartid);

    if (last_pc == virt_machine_get_pc(m, hartid))
        return 0;

    if (m->common.trace) {
        --m->common.trace;
        return keep_going;
    }

    fprintf(dromajo_stderr, "%d %d 0x%016" PRIx64 " (0x%08x)", hartid, priv, last_pc,
            (insn_raw & 3) == 3 ? insn_raw : (uint16_t) insn_raw);

    uint64_t dummy1, dummy2;
    int iregno = riscv_get_most_recently_written_reg(cpu, &dummy1);
    int fregno = riscv_get_most_recently_written_fp_reg(cpu, &dummy2);

    if (cpu->pending_exception != -1)
        fprintf(dromajo_stderr, " exception %d, tval %016lx", cpu->pending_exception,
                riscv_get_priv_level(cpu) == PRV_M ? cpu->mtval : cpu->stval);
    else if (iregno > 0)
        fprintf(dromajo_stderr, " x%2d 0x%016" PRIx64, iregno, virt_machine_get_reg(m, hartid, iregno));
    else if (fregno >= 0)
        fprintf(dromajo_stderr, " f%2d 0x%016" PRIx64, fregno, virt_machine_get_fpreg(m, hartid, fregno));

    putc('\n', dromajo_stderr);

    return keep_going;
}

#ifdef LIVECACHE
extern LiveCache *llc;
#endif

int main(int argc, char **argv)
{
#ifdef LIVECACHE
    //llc = new LiveCache("LLC", 1024*1024*32); // 32MB LLC (should be ~2x larger than real)
    llc = new LiveCache("LLC", 1024*32); // Small 32KB for testing
#endif

#ifdef REGRESS_COSIM
    dromajo_cosim_state_t *costate = 0;
    costate = dromajo_cosim_init(argc, argv);

    if (!costate)
        return 1;

    while (!dromajo_cosim_step(costate, 0, 0, 0, 0, 0, false));
#else
    RISCVMachine *m = virt_machine_main(argc, argv);

    if (!m)
        return 1;

    int keep_going;
    do {
        keep_going = 0;
        for (int i = 0; i < m->ncpus; ++i)
            keep_going |= iterate_core(m, i);
    } while (keep_going);

    for (int i = 0; i < m->ncpus; ++i) {
        int benchmark_exit_code = riscv_benchmark_exit_code(m->cpu_state[i]);
        if (benchmark_exit_code != 0) {
            fprintf(dromajo_stderr, "\nBenchmark exited with code: %i \n", benchmark_exit_code);
            return 1;
        }
    }

    fprintf(dromajo_stderr, "\nPower off.\n");

    virt_machine_end(m);
#endif

#ifdef LIVECACHE
#if 0
    // LiveCache Dump
    int addr_size;
    uint64_t *addr = llc->traverse(addr_size);

    for (int i = 0; i < addr_size; ++i) {
        printf("addr:%llx %s\n", (unsigned long long)addr[i], (addr[i] & 1) ? "ST" : "LD");
    }
#endif
    delete llc;
#endif

    return 0;
}