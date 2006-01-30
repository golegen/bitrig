/*	$OpenBSD: fpu_emulate.c,v 1.14 2006/01/30 21:23:22 miod Exp $	*/
/*	$NetBSD: fpu_emulate.c,v 1.25 2003/09/22 14:18:34 cl Exp $	*/

/*
 * Copyright (c) 1995 Gordon W. Ross
 * some portion Copyright (c) 1995 Ken Nakata
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 4. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Gordon Ross
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * mc68881 emulator
 * XXX - Just a start at it for now...
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <machine/frame.h>

#if defined(DDB) && defined(DEBUG_FPE)
#include <machine/db_machdep.h>
#endif

#include "fpu_emulate.h"

int fpu_emul_fmovmcr(struct fpemu *fe, struct instruction *insn, int *);
int fpu_emul_fmovm(struct fpemu *fe, struct instruction *insn, int *);
int fpu_emul_arith(struct fpemu *fe, struct instruction *insn, int *);
int fpu_emul_type1(struct fpemu *fe, struct instruction *insn, int *);
int fpu_emul_brcc(struct fpemu *fe, struct instruction *insn);
int test_cc(struct fpemu *fe, int pred);
struct fpn *fpu_cmp(struct fpemu *fe);

#if DEBUG_FPE
#  define DUMP_INSN(insn)						\
    printf("fpu_emulate: insn={adv=%d,siz=%d,op=%04x,w1=%04x}\n",	\
	   (insn)->is_advance, (insn)->is_datasize,			\
	   (insn)->is_opcode, (insn)->is_word1)
#else
#  define DUMP_INSN(insn)
#endif

/*
 * Emulate a floating-point instruction.
 * Return zero for success, else signal number.
 * (Typically: zero, SIGFPE, SIGILL, SIGSEGV)
 */
int
fpu_emulate(struct frame *frame, struct fpframe *fpf, int *typ)
{
    static struct instruction insn;
    static struct fpemu fe;
    int optype, sig;
    u_int16_t word;


    /* initialize insn.is_datasize to tell it is *not* initialized */
    insn.is_datasize = -1;

    fe.fe_frame = frame;
    fe.fe_fpframe = fpf;
    fe.fe_fpsr = fpf->fpf_fpsr;
    fe.fe_fpcr = fpf->fpf_fpcr;

#if DEBUG_FPE
    printf("ENTERING fpu_emulate: FPSR=%08x, FPCR=%08x\n",
	   fe.fe_fpsr, fe.fe_fpcr);
#endif

    /* always set this (to avoid a warning) */
    insn.is_pc = frame->f_pc;
    insn.is_nextpc = 0;
    if (frame->f_format == 4) {
	/*
	 * A format 4 is generated by the 68{EC,LC}040.  The PC is
	 * already set to the instruction following the faulting
	 * instruction.  We need to calculate that, anyway.  The
	 * fslw is the PC of the faulted instruction, which is what
	 * we expect to be in f_pc.
	 *
	 * XXX - This is a hack; it assumes we at least know the
	 * sizes of all instructions we run across.
	 * XXX TODO: This may not be true, so we might want to save the PC
	 * in order to restore it later.
	 */
	/* insn.is_nextpc = frame->f_pc; */
	insn.is_pc = frame->f_fmt4.f_fslw;
	frame->f_pc = insn.is_pc;
    }

    if (copyin((void *)insn.is_pc, &word, sizeof(word)) != 0) {
#ifdef DEBUG
	printf("fpu_emulate: fault reading opcode\n");
#endif
	return (SIGSEGV);
    }

    if ((word & 0xf000) != 0xf000) {
#ifdef DEBUG
	printf("fpu_emulate: not coproc. insn.: opcode=0x%x\n", word);
#endif
	return (SIGILL);
    }

    if ((word & 0x0E00) != 0x0200) {
#ifdef DEBUG
	printf("fpu_emulate: bad coproc. id: opcode=0x%x\n", word);
#endif
	*typ = ILL_COPROC;
	return (SIGILL);
    }

    insn.is_opcode = word;
    optype = (word & 0x01C0);

    if (copyin((void *)(insn.is_pc + 2), &word, sizeof(word)) != 0) {
#ifdef DEBUG
	printf("fpu_emulate: fault reading word1\n");
#endif
	return (SIGSEGV);
    }
    insn.is_word1 = word;
    /* all FPU instructions are at least 4-byte long */
    insn.is_advance = 4;

    DUMP_INSN(&insn);

    /*
     * Which family (or type) of opcode is it?
     * Tests ordered by likelihood (hopefully).
     * Certainly, type 0 is the most common.
     */
    if (optype == 0x0000) {
	/* type=0: generic */
	if ((word & 0xc000) == 0xc000) {
#if DEBUG_FPE
	    printf("fpu_emulate: fmovm FPr\n");
#endif
	    sig = fpu_emul_fmovm(&fe, &insn, typ);
	} else if ((word & 0xc000) == 0x8000) {
#if DEBUG_FPE
	    printf("fpu_emulate: fmovm FPcr\n");
#endif
	    sig = fpu_emul_fmovmcr(&fe, &insn, typ);
	} else if ((word & 0xe000) == 0x6000) {
	    /* fstore = fmove FPn,mem */
#if DEBUG_FPE
	    printf("fpu_emulate: fmove to mem\n");
#endif
	    sig = fpu_emul_fstore(&fe, &insn, typ);
	} else if ((word & 0xfc00) == 0x5c00) {
	    /* fmovecr */
#if DEBUG_FPE
	    printf("fpu_emulate: fmovecr\n");
#endif
	    sig = fpu_emul_fmovecr(&fe, &insn, typ);
	} else if ((word & 0xa07f) == 0x26) {
	    /* fscale */
#if DEBUG_FPE
	    printf("fpu_emulate: fscale\n");
#endif
	    sig = fpu_emul_fscale(&fe, &insn, typ);
	} else {
#if DEBUG_FPE
	    printf("fpu_emulate: other type0\n");
#endif
	    /* all other type0 insns are arithmetic */
	    sig = fpu_emul_arith(&fe, &insn, typ);
	}
	if (sig == 0) {
#if DEBUG_FPE
	    printf("fpu_emulate: type 0 returned 0\n");
#endif
	    sig = fpu_upd_excp(&fe);
	}
    } else if (optype == 0x0080 || optype == 0x00C0) {
	/* type=2 or 3: fbcc, short or long disp. */
#if DEBUG_FPE
	printf("fpu_emulate: fbcc %s\n",
	       (optype & 0x40) ? "long" : "short");
#endif
	sig = fpu_emul_brcc(&fe, &insn);
    } else if (optype == 0x0040) {
	/* type=1: fdbcc, fscc, ftrapcc */
#if DEBUG_FPE
	printf("fpu_emulate: type1\n");
#endif
	sig = fpu_emul_type1(&fe, &insn, typ);
    } else {
	/* type=4: fsave    (privileged) */
	/* type=5: frestore (privileged) */
	/* type=6: reserved */
	/* type=7: reserved */
#ifdef DEBUG
	printf("fpu_emulate: bad opcode type: opcode=0x%x\n", insn.is_opcode);
#endif
	*typ = ILL_PRVOPC;
	sig = SIGILL;
    }

    DUMP_INSN(&insn);

     /*
      * XXX it is not clear to me, if we should progress the PC always,
      * for SIGFPE || 0, or only for 0; however, without SIGFPE, we
      * don't pass the signalling regression  tests.	-is
      */
    if ((sig == 0) || (sig == SIGFPE))
	frame->f_pc += insn.is_advance;
#if defined(DDB) && defined(DEBUG_FPE)
    else {
	printf("fpu_emulate: sig=%d, opcode=%x, word1=%x\n",
	       sig, insn.is_opcode, insn.is_word1);
	kdb_trap(-1, (db_regs_t *)frame);
    }
#endif
#if 0 /* XXX something is wrong */
    if (frame->f_format == 4) {
	/* XXX Restore PC -- 68{EC,LC}040 only */
	if (insn.is_nextpc)
		frame->f_pc = insn.is_nextpc;
    }
#endif

#if DEBUG_FPE
    printf("EXITING fpu_emulate: w/FPSR=%08x, FPCR=%08x\n",
	   fe.fe_fpsr, fe.fe_fpcr);
#endif

    if (*typ == 0)
	switch (sig) {
	case SIGSEGV:
		*typ = SEGV_MAPERR;
		break;
	case SIGILL:
		*typ = ILL_ILLOPC;
		break;
	case SIGFPE:
		*typ = FPE_FLTINV;
		break;
	}
    return (sig);
}

/* update accrued exception bits and see if there's an FP exception */
int
fpu_upd_excp(fe)
     struct fpemu *fe;
{
    u_int fpsr;
    u_int fpcr;

    fpsr = fe->fe_fpsr;
    fpcr = fe->fe_fpcr;
    /* update fpsr accrued exception bits; each insn doesn't have to
       update this */
    if (fpsr & (FPSR_BSUN | FPSR_SNAN | FPSR_OPERR)) {
	fpsr |= FPSR_AIOP;
    }
    if (fpsr & FPSR_OVFL) {
	fpsr |= FPSR_AOVFL;
    }
    if ((fpsr & FPSR_UNFL) && (fpsr & FPSR_INEX2)) {
	fpsr |= FPSR_AUNFL;
    }
    if (fpsr & FPSR_DZ) {
	fpsr |= FPSR_ADZ;
    }
    if (fpsr & (FPSR_INEX1 | FPSR_INEX2 | FPSR_OVFL)) {
	fpsr |= FPSR_AINEX;
    }

    fe->fe_fpframe->fpf_fpsr = fe->fe_fpsr = fpsr;

    return (fpsr & fpcr & FPSR_EXCP) ? SIGFPE : 0;
}

/* update fpsr according to fp (= result of an fp op) */
u_int
fpu_upd_fpsr(fe, fp)
     struct fpemu *fe;
     struct fpn *fp;
{
    u_int fpsr;

#if DEBUG_FPE
    printf("fpu_upd_fpsr: previous fpsr=%08x\n", fe->fe_fpsr);
#endif
    /* clear all condition code */
    fpsr = fe->fe_fpsr & ~FPSR_CCB;

#if DEBUG_FPE
    printf("fpu_upd_fpsr: result is a ");
#endif
    if (fp->fp_sign) {
#if DEBUG_FPE
	printf("negative ");
#endif
	fpsr |= FPSR_NEG;
#if DEBUG_FPE
    } else {
	printf("positive ");
#endif
    }

    switch (fp->fp_class) {
    case FPC_SNAN:
#if DEBUG_FPE
	printf("signaling NAN\n");
#endif
	fpsr |= (FPSR_NAN | FPSR_SNAN);
	break;
    case FPC_QNAN:
#if DEBUG_FPE
	printf("quiet NAN\n");
#endif
	fpsr |= FPSR_NAN;
	break;
    case FPC_ZERO:
#if DEBUG_FPE
	printf("Zero\n");
#endif
	fpsr |= FPSR_ZERO;
	break;
    case FPC_INF:
#if DEBUG_FPE
	printf("Inf\n");
#endif
	fpsr |= FPSR_INF;
	break;
    default:
#if DEBUG_FPE
	printf("Number\n");
#endif
	/* anything else is treated as if it is a number */
	break;
    }

    fe->fe_fpsr = fe->fe_fpframe->fpf_fpsr = fpsr;

#if DEBUG_FPE
    printf("fpu_upd_fpsr: new fpsr=%08x\n", fe->fe_fpframe->fpf_fpsr);
#endif

    return fpsr;
}

int
fpu_emul_fmovmcr(struct fpemu *fe, struct instruction *insn, int *typ)
{
    struct frame *frame = fe->fe_frame;
    struct fpframe *fpf = fe->fe_fpframe;
    int sig;
    int reglist;
    int fpu_to_mem;

    /* move to/from control registers */
    reglist = (insn->is_word1 & 0x1c00) >> 10;
    /* Bit 13 selects direction (FPU to/from Mem) */
    fpu_to_mem = insn->is_word1 & 0x2000;

    insn->is_datasize = 4;
    insn->is_advance = 4;
    sig = fpu_decode_ea(frame, insn, &insn->is_ea, insn->is_opcode, typ);
    if (sig) { return sig; }

    if (reglist != 1 && reglist != 2 && reglist != 4 &&
	(insn->is_ea.ea_flags & EA_DIRECT)) {
	/* attempted to copy more than one FPcr to CPU regs */
#ifdef DEBUG
	printf("fpu_emul_fmovmcr: tried to copy too many FPcr\n");
#endif
	return SIGILL;
    }

    if (reglist & 4) {
	/* fpcr */
	if ((insn->is_ea.ea_flags & EA_DIRECT) &&
	    insn->is_ea.ea_regnum >= 8 /* address reg */) {
	    /* attempted to copy FPCR to An */
#ifdef DEBUG
	    printf("fpu_emul_fmovmcr: tried to copy FPCR from/to A%d\n",
		   insn->is_ea.ea_regnum & 7);
#endif
	    return SIGILL;
	}
	if (fpu_to_mem) {
	    sig = fpu_store_ea(frame, insn, &insn->is_ea,
			       (char *)&fpf->fpf_fpcr);
	} else {
	    sig = fpu_load_ea(frame, insn, &insn->is_ea,
			      (char *)&fpf->fpf_fpcr, typ);
	}
    }
    if (sig) { return sig; }

    if (reglist & 2) {
	/* fpsr */
	if ((insn->is_ea.ea_flags & EA_DIRECT) &&
	    insn->is_ea.ea_regnum >= 8 /* address reg */) {
	    /* attempted to copy FPSR to An */
#ifdef DEBUG
	    printf("fpu_emul_fmovmcr: tried to copy FPSR from/to A%d\n",
		   insn->is_ea.ea_regnum & 7);
#endif
	    return SIGILL;
	}
	if (fpu_to_mem) {
	    sig = fpu_store_ea(frame, insn, &insn->is_ea,
			       (char *)&fpf->fpf_fpsr);
	} else {
	    sig = fpu_load_ea(frame, insn, &insn->is_ea,
			      (char *)&fpf->fpf_fpsr, typ);
	}
    }
    if (sig) { return sig; }
  
    if (reglist & 1) {
	/* fpiar - can be moved to/from An */
	if (fpu_to_mem) {
	    sig = fpu_store_ea(frame, insn, &insn->is_ea,
			       (char *)&fpf->fpf_fpiar);
	} else {
	    sig = fpu_load_ea(frame, insn, &insn->is_ea,
			      (char *)&fpf->fpf_fpiar, typ);
	}
    }
    return sig;
}

/*
 * type 0: fmovem
 * Separated out of fpu_emul_type0 for efficiency.
 * In this function, we know:
 *   (opcode & 0x01C0) == 0
 *   (word1 & 0x8000) == 0x8000
 *
 * No conversion or rounding is done by this instruction,
 * and the FPSR is not affected.
 */
int
fpu_emul_fmovm(struct fpemu *fe, struct instruction *insn, int *typ)
{
    struct frame *frame = fe->fe_frame;
    struct fpframe *fpf = fe->fe_fpframe;
    int word1, sig;
    int reglist, regmask, regnum;
    int fpu_to_mem, order;
    int w1_post_incr;
    int *fpregs;

    insn->is_advance = 4;
    insn->is_datasize = 12;
    word1 = insn->is_word1;

    /* Bit 13 selects direction (FPU to/from Mem) */
    fpu_to_mem = word1 & 0x2000;

    /*
     * Bits 12,11 select register list mode:
     * 0,0: Static  reg list, pre-decr.
     * 0,1: Dynamic reg list, pre-decr.
     * 1,0: Static  reg list, post-incr.
     * 1,1: Dynamic reg list, post-incr
     */
    w1_post_incr = word1 & 0x1000;
    if (word1 & 0x0800) {
	/* dynamic reg list */
	reglist = frame->f_regs[(word1 & 0x70) >> 4];
    } else {
	reglist = word1;
    }
    reglist &= 0xFF;

    /* Get effective address. (modreg=opcode&077) */
    sig = fpu_decode_ea(frame, insn, &insn->is_ea, insn->is_opcode, typ);
    if (sig) { return sig; }

    /* Get address of soft coprocessor regs. */
    fpregs = &fpf->fpf_regs[0];

    if (insn->is_ea.ea_flags & EA_PREDECR) {
	regnum = 7;
	order = -1;
    } else {
	regnum = 0;
	order = 1;
    }

    regmask = 0x80;
    while ((0 <= regnum) && (regnum < 8)) {
	if (regmask & reglist) {
	    if (fpu_to_mem) {
		sig = fpu_store_ea(frame, insn, &insn->is_ea,
				   (char *)&fpregs[regnum * 3]);
#if DEBUG_FPE
		printf("fpu_emul_fmovm: FP%d (%08x,%08x,%08x) saved\n",
		       regnum, fpregs[regnum * 3], fpregs[regnum * 3 + 1],
		       fpregs[regnum * 3 + 2]);
#endif
	    } else {		/* mem to fpu */
		sig = fpu_load_ea(frame, insn, &insn->is_ea,
				  (char *)&fpregs[regnum * 3], typ);
#if DEBUG_FPE
		printf("fpu_emul_fmovm: FP%d (%08x,%08x,%08x) loaded\n",
		       regnum, fpregs[regnum * 3], fpregs[regnum * 3 + 1],
		       fpregs[regnum * 3 + 2]);
#endif
	    }
	    if (sig) { break; }
	}
	regnum += order;
	regmask >>= 1;
    }

    return sig;
}

struct fpn *
fpu_cmp(fe)
     struct fpemu *fe;
{
    struct fpn *x = &fe->fe_f1, *y = &fe->fe_f2;

    /* take care of special cases */
    if (x->fp_class < 0 || y->fp_class < 0) {
	/* if either of two is a SNAN, result is SNAN */
	x->fp_class = (y->fp_class < x->fp_class) ? y->fp_class : x->fp_class;
    } else if (x->fp_class == FPC_INF) {
	if (y->fp_class == FPC_INF) {
	    /* both infinities */
	    if (x->fp_sign == y->fp_sign) {
		x->fp_class = FPC_ZERO;	/* return a signed zero */
	    } else {
		x->fp_class = FPC_NUM; /* return a faked number w/x's sign */
		x->fp_exp = 16383;
		x->fp_mant[0] = FP_1;
	    }
	} else {
	    /* y is a number */
	    x->fp_class = FPC_NUM; /* return a forged number w/x's sign */
	    x->fp_exp = 16383;
	    x->fp_mant[0] = FP_1;
	}
    } else if (y->fp_class == FPC_INF) {
	/* x is a Num but y is an Inf */
	/* return a forged number w/y's sign inverted */
	x->fp_class = FPC_NUM;
	x->fp_sign = !y->fp_sign;
	x->fp_exp = 16383;
	x->fp_mant[0] = FP_1;
    } else {
	/* x and y are both numbers or zeros, or pair of a number and a zero */
	y->fp_sign = !y->fp_sign;
	x = fpu_add(fe);	/* (x - y) */
	/*
	 * FCMP does not set Inf bit in CC, so return a forged number
	 * (value doesn't matter) if Inf is the result of fsub.
	 */
	if (x->fp_class == FPC_INF) {
	    x->fp_class = FPC_NUM;
	    x->fp_exp = 16383;
	    x->fp_mant[0] = FP_1;
	}
    }
    return x;
}

/*
 * arithmetic oprations
 */
int
fpu_emul_arith(struct fpemu *fe, struct instruction *insn, int *typ)
{
    struct frame *frame = fe->fe_frame;
    u_int *fpregs = &(fe->fe_fpframe->fpf_regs[0]);
    struct fpn *res;
    int word1, sig = 0;
    int regnum, format;
    int discard_result = 0;
    u_int buf[3];
#if DEBUG_FPE
    int flags;
    char regname;
#endif

    fe->fe_fpsr &= ~FPSR_EXCP;

    DUMP_INSN(insn);

#if DEBUG_FPE
    printf("fpu_emul_arith: FPSR = %08x, FPCR = %08x\n",
	   fe->fe_fpsr, fe->fe_fpcr);
#endif

    word1 = insn->is_word1;
    format = (word1 >> 10) & 7;
    regnum = (word1 >> 7) & 7;

    /* fetch a source operand : may not be used */
#if DEBUG_FPE
    printf("fpu_emul_arith: dst/src FP%d=%08x,%08x,%08x\n",
	   regnum, fpregs[regnum*3], fpregs[regnum*3+1],
	   fpregs[regnum*3+2]);
#endif

    fpu_explode(fe, &fe->fe_f1, FTYPE_EXT, &fpregs[regnum * 3]);

    DUMP_INSN(insn);

    /* get the other operand which is always the source */
    if ((word1 & 0x4000) == 0) {
#if DEBUG_FPE
	printf("fpu_emul_arith: FP%d op FP%d => FP%d\n",
	       format, regnum, regnum);
	printf("fpu_emul_arith: src opr FP%d=%08x,%08x,%08x\n",
	       format, fpregs[format*3], fpregs[format*3+1],
	       fpregs[format*3+2]);
#endif
	fpu_explode(fe, &fe->fe_f2, FTYPE_EXT, &fpregs[format * 3]);
    } else {
	/* the operand is in memory */
	if (format == FTYPE_DBL) {
	    insn->is_datasize = 8;
	} else if (format == FTYPE_SNG || format == FTYPE_LNG) {
	    insn->is_datasize = 4;
	} else if (format == FTYPE_WRD) {
	    insn->is_datasize = 2;
	} else if (format == FTYPE_BYT) {
	    insn->is_datasize = 1;
	} else if (format == FTYPE_EXT) {
	    insn->is_datasize = 12;
	} else {
	    /* invalid or unsupported operand format */
	    *typ = ILL_ILLOPN;
	    sig = SIGILL;
	    return sig;
	}

	/* Get effective address. (modreg=opcode&077) */
	sig = fpu_decode_ea(frame, insn, &insn->is_ea, insn->is_opcode, typ);
	if (sig) {
#if DEBUG_FPE
	    printf("fpu_emul_arith: error in fpu_decode_ea\n");
#endif
	    return sig;
	}

	DUMP_INSN(insn);

#if DEBUG_FPE
	printf("fpu_emul_arith: addr mode = ");
	flags = insn->is_ea.ea_flags;
	regname = (insn->is_ea.ea_regnum & 8) ? 'a' : 'd';

	if (flags & EA_DIRECT) {
	    printf("%c%d\n",
		   regname, insn->is_ea.ea_regnum & 7);
	} else if (flags & EA_PC_REL) {
	    if (flags & EA_OFFSET) {
		printf("pc@(%d)\n", insn->is_ea.ea_offset);
	    } else if (flags & EA_INDEXED) {
		printf("pc@(...)\n");
	    }
	} else if (flags & EA_PREDECR) {
	    printf("%c%d@-\n",
		   regname, insn->is_ea.ea_regnum & 7);
	} else if (flags & EA_POSTINCR) {
	    printf("%c%d@+\n", regname, insn->is_ea.ea_regnum & 7);
	} else if (flags & EA_OFFSET) {
	    printf("%c%d@(%d)\n", regname, insn->is_ea.ea_regnum & 7,
		   insn->is_ea.ea_offset);
	} else if (flags & EA_INDEXED) {
	    printf("%c%d@(...)\n", regname, insn->is_ea.ea_regnum & 7);
	} else if (flags & EA_ABS) {
	    printf("0x%08x\n", insn->is_ea.ea_absaddr);
	} else if (flags & EA_IMMED) {

	    printf("#0x%08x,%08x,%08x\n", insn->is_ea.ea_immed[0],
		   insn->is_ea.ea_immed[1], insn->is_ea.ea_immed[2]);
	} else {
	    printf("%c%d@\n", regname, insn->is_ea.ea_regnum & 7);
	}
#endif /* DEBUG_FPE */

	fpu_load_ea(frame, insn, &insn->is_ea, (char *)buf, typ);
	if (format == FTYPE_WRD) {
	    /* sign-extend */
	    buf[0] &= 0xffff;
	    if (buf[0] & 0x8000) {
		buf[0] |= 0xffff0000;
	    }
	    format = FTYPE_LNG;
	} else if (format == FTYPE_BYT) {
	    /* sign-extend */
	    buf[0] &= 0xff;
	    if (buf[0] & 0x80) {
		buf[0] |= 0xffffff00;
	    }
	    format = FTYPE_LNG;
	}
#if DEBUG_FPE
	printf("fpu_emul_arith: src = %08x %08x %08x, siz = %d\n",
	       buf[0], buf[1], buf[2], insn->is_datasize);
#endif
	fpu_explode(fe, &fe->fe_f2, format, buf);
    }

    DUMP_INSN(insn);

    /* An arithmetic instruction emulate function has a prototype of
     * struct fpn *fpu_op(struct fpemu *);
     
     * 1) If the instruction is monadic, then fpu_op() must use
     * fe->fe_f2 as its operand, and return a pointer to the
     * result.
     
     * 2) If the instruction is diadic, then fpu_op() must use
     * fe->fe_f1 and fe->fe_f2 as its two operands, and return a
     * pointer to the result.
     
     */
    res = 0;
    switch (word1 & 0x3f) {
    case 0x00:			/* fmove */
	res = &fe->fe_f2;
	break;

    case 0x01:			/* fint */
	res = fpu_int(fe);
	break;

    case 0x03:			/* fintrz */
	res = fpu_intrz(fe);
	break;

    case 0x04:			/* fsqrt */
	res = fpu_sqrt(fe);
	break;

    case 0x06:			/* flognp1 */
	res = fpu_lognp1(fe);
	break;

    case 0x14:			/* flogn */
	res = fpu_logn(fe);
	break;

    case 0x15:			/* flog10 */
	res = fpu_log10(fe);
	break;

    case 0x16:			/* flog2 */
	res = fpu_log2(fe);
	break;

    case 0x18:			/* fabs */
	fe->fe_f2.fp_sign = 0;
	res = &fe->fe_f2;
	break;

    case 0x1A:			/* fneg */
	fe->fe_f2.fp_sign = !fe->fe_f2.fp_sign;
	res = &fe->fe_f2;
	break;

    case 0x1E:			/* fgetexp */
	res = fpu_getexp(fe);
	break;

    case 0x1F:			/* fgetman */
	res = fpu_getman(fe);
	break;

    case 0x20:			/* fdiv */
    case 0x24:			/* fsgldiv: cheating - better than nothing */
	res = fpu_div(fe);
	break;

    case 0x21:			/* fmod */
	res = fpu_mod(fe);
	break;

    case 0x28:			/* fsub */
	fe->fe_f2.fp_sign = !fe->fe_f2.fp_sign; /* f2 = -f2 */
    case 0x22:			/* fadd */
	res = fpu_add(fe);
	break;

    case 0x23:			/* fmul */
    case 0x27:			/* fsglmul: cheating - better than nothing */
	res = fpu_mul(fe);
	break;

    case 0x25:			/* frem */
	res = fpu_rem(fe);
	break;

    case 0x26:
	/* fscale is handled by a separate function */
	break;

    case 0x38:			/* fcmp */
	res = fpu_cmp(fe);
	discard_result = 1;
	break;

    case 0x3A:			/* ftst */
	res = &fe->fe_f2;
	discard_result = 1;
	break;

    case 0x02:			/* fsinh */
    case 0x08:			/* fetoxm1 */
    case 0x09:			/* ftanh */
    case 0x0A:			/* fatan */
    case 0x0C:			/* fasin */
    case 0x0D:			/* fatanh */
    case 0x0E:			/* fsin */
    case 0x0F:			/* ftan */
    case 0x10:			/* fetox */
    case 0x11:			/* ftwotox */
    case 0x12:			/* ftentox */
    case 0x19:			/* fcosh */
    case 0x1C:			/* facos */
    case 0x1D:			/* fcos */
    case 0x30:			/* fsincos */
    case 0x31:			/* fsincos */
    case 0x32:			/* fsincos */
    case 0x33:			/* fsincos */
    case 0x34:			/* fsincos */
    case 0x35:			/* fsincos */
    case 0x36:			/* fsincos */
    case 0x37:			/* fsincos */
    default:
#ifdef DEBUG
	printf("fpu_emul_arith: bad opcode=0x%x, word1=0x%x\n",
	       insn->is_opcode, insn->is_word1);
#endif
	sig = SIGILL;
    } /* switch (word1 & 0x3f) */

    if (!discard_result && sig == 0) {
	fpu_implode(fe, res, FTYPE_EXT, &fpregs[regnum * 3]);
#if DEBUG_FPE
	printf("fpu_emul_arith: %08x,%08x,%08x stored in FP%d\n",
	       fpregs[regnum*3], fpregs[regnum*3+1],
	       fpregs[regnum*3+2], regnum);
    } else if (sig == 0) {
	static char *class_name[] = { "SNAN", "QNAN", "ZERO", "NUM", "INF" };
	printf("fpu_emul_arith: result(%s,%c,%d,%08x,%08x,%08x) discarded\n",
	       class_name[res->fp_class + 2],
	       res->fp_sign ? '-' : '+', res->fp_exp,
	       res->fp_mant[0], res->fp_mant[1],
	       res->fp_mant[2]);
    } else {
	printf("fpu_emul_arith: received signal %d\n", sig);
#endif
    }

    /* update fpsr according to the result of operation */
    fpu_upd_fpsr(fe, res);

#if DEBUG_FPE
    printf("fpu_emul_arith: FPSR = %08x, FPCR = %08x\n",
	   fe->fe_fpsr, fe->fe_fpcr);
#endif

    DUMP_INSN(insn);

    return sig;
}

/* test condition code according to the predicate in the opcode.
 * returns -1 when the predicate evaluates to true, 0 when false.
 * signal numbers are returned when an error is detected.
 */
int
test_cc(fe, pred)
     struct fpemu *fe;
     int pred;
{
    int result, sig_bsun, invert;
    int fpsr;

    fpsr = fe->fe_fpsr;
    invert = 0;
    fpsr &= ~FPSR_EXCP;		/* clear all exceptions */
#if DEBUG_FPE
    printf("test_cc: fpsr=0x%08x\n", fpsr);
#endif
    pred &= 0x3f;		/* lowest 6 bits */

#if DEBUG_FPE
    printf("test_cc: ");
#endif

    if (pred >= 0x20) {
	return SIGILL;
    } else if (pred & 0x10) {
	/* IEEE nonaware tests */
	sig_bsun = 1;
	pred &= 0x0f;		/* lower 4 bits */
    } else {
	/* IEEE aware tests */
#if DEBUG_FPE
	printf("IEEE ");
#endif
	sig_bsun = 0;
    }

    if (pred & 0x08) {
#if DEBUG_FPE
	printf("Not ");
#endif
	/* predicate is "NOT ..." */
	pred ^= 0xf;		/* invert */
	invert = -1;
    }
    switch (pred) {
    case 0:			/* (Signaling) False */
#if DEBUG_FPE
	printf("False");
#endif
	result = 0;
	break;
    case 1:			/* (Signaling) Equal */
#if DEBUG_FPE
	printf("Equal");
#endif
	result = -((fpsr & FPSR_ZERO) == FPSR_ZERO);
	break;
    case 2:			/* Greater Than */
#if DEBUG_FPE
	printf("GT");
#endif
	result = -((fpsr & (FPSR_NAN|FPSR_ZERO|FPSR_NEG)) == 0);
	break;
    case 3:			/* Greater or Equal */
#if DEBUG_FPE
	printf("GE");
#endif
	result = -((fpsr & FPSR_ZERO) ||
		   (fpsr & (FPSR_NAN|FPSR_NEG)) == 0);
	break;
    case 4:			/* Less Than */
#if DEBUG_FPE
	printf("LT");
#endif
	result = -((fpsr & (FPSR_NAN|FPSR_ZERO|FPSR_NEG)) == FPSR_NEG);
	break;
    case 5:			/* Less or Equal */
#if DEBUG_FPE
	printf("LE");
#endif
	result = -((fpsr & FPSR_ZERO) ||
		   ((fpsr & (FPSR_NAN|FPSR_NEG)) == FPSR_NEG));
	break;
    case 6:			/* Greater or Less than */
#if DEBUG_FPE
	printf("GLT");
#endif
	result = -((fpsr & (FPSR_NAN|FPSR_ZERO)) == 0);
	break;
    case 7:			/* Greater, Less or Equal */
#if DEBUG_FPE
	printf("GLE");
#endif
	result = -((fpsr & FPSR_NAN) == 0);
	break;
    default:
	/* invalid predicate */
	return SIGILL;
    }
    result ^= invert;		/* if the predicate is "NOT ...", then
				   invert the result */
#if DEBUG_FPE
    printf("=> %s (%d)\n", result ? "true" : "false", result);
#endif
    /* if it's an IEEE unaware test and NAN is set, BSUN is set */
    if (sig_bsun && (fpsr & FPSR_NAN)) {
	fpsr |= FPSR_BSUN;
    }

    /* put fpsr back */
    fe->fe_fpframe->fpf_fpsr = fe->fe_fpsr = fpsr;

    return result;
}

/*
 * type 1: fdbcc, fscc, ftrapcc
 * In this function, we know:
 *   (opcode & 0x01C0) == 0x0040
 */
int
fpu_emul_type1(struct fpemu *fe, struct instruction *insn, int *typ)
{
    struct frame *frame = fe->fe_frame;
    int advance, sig, branch;
    int16_t displ;

    branch = test_cc(fe, insn->is_word1);
    fe->fe_fpframe->fpf_fpsr = fe->fe_fpsr;

    insn->is_advance = 4;
    sig = 0;

    switch (insn->is_opcode & 070) {
    case 010:			/* fdbcc */
	if (branch == -1) {
	    /* advance */
	    insn->is_advance = 6;
	} else if (!branch) {
	    /* decrement Dn and if (Dn != -1) branch */
	    u_int16_t count = frame->f_regs[insn->is_opcode & 7];

	    if (count-- != 0) {
		if (copyin((void *)(insn->is_pc + insn->is_advance), &displ,
		    sizeof(displ)) != 0) {
#ifdef DEBUG
		    printf("fpu_emul_type1: fault reading displacement\n");
#endif
		    return SIGSEGV;
		}
		insn->is_advance += (int)displ;
		/* XXX insn->is_nextpc = insn->is_pc + insn->is_advance; */
	    } else {
		insn->is_advance = 6;
	    }
	    /* write it back */
	    frame->f_regs[insn->is_opcode & 7] &= 0xffff0000;
	    frame->f_regs[insn->is_opcode & 7] |= (u_int32_t)count;
	} else {		/* got a signal */
	    sig = SIGFPE;
	}
	break;

    case 070:			/* ftrapcc or fscc */
	advance = 4;
	if ((insn->is_opcode & 07) >= 2) {
	    switch (insn->is_opcode & 07) {
	    case 3:		/* long opr */
		advance += 2;
	    case 2:		/* word opr */
		advance += 2;
	    case 4:		/* no opr */
		break;
	    default:
		return SIGILL;
		break;
	    }

	    if (branch == 0) {
		/* no trap */
		insn->is_advance = advance;
		sig = 0;
	    } else {
		/* trap */
		sig = SIGILL;
		*typ = ILL_ILLTRP;
	    }
	    break;
	} /* if ((insn->is_opcode & 7) < 2), fall through to FScc */

    default:			/* fscc */
	insn->is_advance = 4;
	insn->is_datasize = 1;	/* always byte */
	sig = fpu_decode_ea(frame, insn, &insn->is_ea, insn->is_opcode, typ);
	if (sig) {
	    break;
	}
	if (branch == -1 || branch == 0) {
	    /* set result */
	    sig = fpu_store_ea(frame, insn, &insn->is_ea, (char *)&branch);
	} else {
	    /* got an exception */
	    sig = branch;
	}
	break;
    }
    return sig;
}

/*
 * Type 2 or 3: fbcc (also fnop)
 * In this function, we know:
 *   (opcode & 0x0180) == 0x0080
 */
int
fpu_emul_brcc(fe, insn)
     struct fpemu *fe;
     struct instruction *insn;
{
    int displ;
    int sig;
    u_int16_t word2;

    /*
     * Get branch displacement.
     */
    insn->is_advance = 4;
    displ = insn->is_word1;

    if (insn->is_opcode & 0x40) {
	if (copyin((void *)(insn->is_pc + insn->is_advance), &word2,
	    sizeof(word2)) != 0) {
#ifdef DEBUG
	    printf("fpu_emul_brcc: fault reading word2\n");
#endif
	    return SIGSEGV;
	}
	displ <<= 16;
	displ |= word2;
	insn->is_advance += 2;
    } else /* displacement is word sized */
        if (displ & 0x8000)
	    displ |= 0xFFFF0000;

    /* XXX: If CC, insn->is_pc += displ */
    sig = test_cc(fe, insn->is_opcode);
    fe->fe_fpframe->fpf_fpsr = fe->fe_fpsr;

    if (fe->fe_fpsr & fe->fe_fpcr & FPSR_EXCP) {
	return SIGFPE;		/* caught an exception */
    }
    if (sig == -1) {
	/* branch does take place; 2 is the offset to the 1st disp word */
	insn->is_advance = displ + 2;
	/* XXX insn->is_nextpc = insn->is_pc + insn->is_advance; */
    } else if (sig) {
	return SIGILL;		/* got a signal */
    }
#if DEBUG_FPE
    printf("fpu_emul_brcc: %s insn @ %x (%x+%x) (disp=%x)\n",
	   (sig == -1) ? "BRANCH to" : "NEXT",
	   insn->is_pc + insn->is_advance, insn->is_pc, insn->is_advance,
	   displ);
#endif
    return 0;
}
