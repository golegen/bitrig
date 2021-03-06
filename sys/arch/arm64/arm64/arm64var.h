/*
 * Copyright (c) 2005,2008 Dale Rahn <drahn@openbsd.com>
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __ARM64VAR_H__
#define __ARM64VAR_H__

struct arm64mem {
        bus_addr_t      addr;
        bus_size_t      size;
};

#define ARM64_DEV_NMEM	4
#define ARM64_DEV_NIRQ	4
#define ARM64_DEV_NDMA	2

struct arm64_dev {
	char	*name;			/* driver name or made up name */
	int	unit;			/* driver instance number or -1 */
	struct	arm64mem mem[ARM64_DEV_NMEM]; /* memory ranges */
	int	irq[ARM64_DEV_NIRQ];	/* IRQ number(s) */
	int	dma[ARM64_DEV_NDMA];	/* DMA chan number(s) */
};

/* Passed as third arg to attach functions. */
struct arm64_attach_args {
        struct arm64_dev        *aa_dev;
        bus_space_tag_t         aa_iot;
        bus_dma_tag_t           aa_dmat;
        void                    *aa_node;
};

extern bus_space_t arm64_bs_tag;
extern bus_space_t arm64_a4x_bs_tag;

#endif /* __ARM64VAR_H__ */

