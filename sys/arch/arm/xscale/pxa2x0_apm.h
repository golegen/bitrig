/*	$OpenBSD: pxa2x0_apm.h,v 1.3 2005/01/26 06:34:53 uwe Exp $	*/

/*
 * Copyright (c) 2005 Uwe Stuehler <uwe@bsdx.de>
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

#ifndef _PXA2X0_APM_H_
#define _PXA2X0_APM_H_

#include <sys/event.h>

#include <machine/bus.h>
#include <machine/apmvar.h>

struct pxa2x0_apm_softc {
	struct	device sc_dev;
	struct	proc *sc_thread;
	struct	lock sc_lock;
	struct	klist sc_note;
	int	sc_flags;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_pm_ioh;
	void	(*sc_periodic_check)(struct pxa2x0_apm_softc *);
	void	(*sc_power_info)(struct pxa2x0_apm_softc *,
	    struct apm_power_info *);
};

extern	void pxa2x0_apm_attach_sub(struct pxa2x0_apm_softc *);

#endif
