/*	$OpenBSD: zaurus_scoop.c,v 1.4 2005/01/26 06:34:54 uwe Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>

#include <machine/bus.h>

#include <arm/xscale/pxa2x0var.h>

#include <zaurus/dev/zaurus_scoopreg.h>
#include <zaurus/dev/zaurus_scoopvar.h>

struct scoop_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
};

int	scoopmatch(struct device *, void *, void *);
void	scoopattach(struct device *, struct device *, void *);

struct cfattach scoop_pxaip_ca = {
	sizeof (struct scoop_softc), scoopmatch, scoopattach
};

struct cfdriver scoop_cd = {
	NULL, "scoop", DV_DULL
};

int	scoop_gpio_pin_read(struct scoop_softc *sc, int);
void	scoop_gpio_pin_write(struct scoop_softc *sc, int, int);


int
scoopmatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;

	/*
	 * Only the C3000 models (pxa270) are known to have two SCOOPs,
	 * on other models we only find the first one.
	 */
        if ((cputype & ~CPU_ID_XSCALE_COREREV_MASK) == CPU_ID_PXA27X)
	    	return (cf->cf_unit < 2);

	return (cf->cf_unit == 0);
}

void
scoopattach(struct device *parent, struct device *self, void *aux)
{
	struct pxaip_attach_args *pxa = aux;
	struct scoop_softc *sc = (struct scoop_softc *)self;
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_iot = pxa->pxa_iot;

	if (pxa->pxa_addr != -1)
		addr = pxa->pxa_addr;
	else if (sc->sc_dev.dv_unit == 0)
		addr = SCOOP0_BASE;
	else
		addr = SCOOP1_BASE;

	size = pxa->pxa_size < SCOOP_SIZE ? SCOOP_SIZE : pxa->pxa_size;

	if (bus_space_map(sc->sc_iot, addr, size, 0, &sc->sc_ioh) != 0) {
		printf(": failed to map %s\n", sc->sc_dev.dv_xname);
		return;
	}

	printf(": Onboard Peripheral Controller\n");
}

int
scoop_gpio_pin_read(struct scoop_softc *sc, int pin)
{
	unsigned short rv;
	unsigned short bit = (1 << pin);

	rv = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR);
	return (rv & bit) != 0 ? 1 : 0;
}

void
scoop_gpio_pin_write(struct scoop_softc *sc, int pin, int value)
{
	unsigned short rv;
	unsigned short bit = (1 << pin);

	rv = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, SCOOP_GPWR,
	    value != 0 ? (rv | bit) : (rv & ~bit));
}

void
scoop_backlight_set(int on)
{

#if 0	/* XXX no effect. maybe the pin is incorrectly configured? */
	if (scoop_cd.cd_ndevs > 1 && scoop_cd.cd_devs[1] != NULL)
		scoop_gpio_pin_write(scoop_cd.cd_devs[1],
		    SCOOP1_BACKLIGHT_ON, on);
#endif
}

void
scoop_led_set(int led, int on)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL) {
		switch(led) {
		case SCOOP_LED_GREEN:
			scoop_gpio_pin_write(scoop_cd.cd_devs[0],
			    SCOOP0_LED_GREEN_C3000, on);
			break;
		case SCOOP_LED_ORANGE:
			scoop_gpio_pin_write(scoop_cd.cd_devs[0],
			    SCOOP0_LED_ORANGE_C3000, on);
			break;
		}
	}
}

void
scoop_battery_temp_adc(int enable)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL)
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_ADC_TEMP_ON_C3000, enable);
}

void
scoop_charge_battery(int enable, int voltage_high)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL) {
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_JK_B_C3000, voltage_high);
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_CHARGE_OFF_C3000, !enable);
	}
}

void
scoop_discharge_battery(int enable)
{

	if (scoop_cd.cd_ndevs > 0 && scoop_cd.cd_devs[0] != NULL)
		scoop_gpio_pin_write(scoop_cd.cd_devs[0],
		    SCOOP0_JK_A_C3000, enable);
}
