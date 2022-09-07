/*
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#include "hw/ssi/spi_gpio.h"
#include "hw/irq.h"

#define SPI_CPHA BIT(0) /* clock phase (1 = SPI_CLOCK_PHASE_SECOND) */
#define SPI_CPOL BIT(1) /* clock polarity (1 = SPI_POLARITY_HIGH) */

static void do_leading_edge(SpiGpioState *s)
{
    if (!s->CPHA) {
        s->input_bits |= object_property_get_bool(OBJECT(s->controller_state),
                                                  "gpioX4", NULL);
        /*
         * According to SPI protocol:
         * CPHA=0 leading half clock cycle is sampling phase
         * We technically should not drive out miso
         * However, when the kernel bitbang driver is setting the clk pin,
         * it will overwrite the miso value, so we are driving out miso in
         * the sampling half clock cycle as well to workaround this issue
         */
        s->miso = !!(s->output_bits & 0x80);
        object_property_set_bool(OBJECT(s->controller_state), "gpioX5", s->miso,
                                 NULL);
    }
}

static void do_trailing_edge(SpiGpioState *s)
{
    if (s->CPHA) {
        s->input_bits |= object_property_get_bool(OBJECT(s->controller_state),
                                                  "gpioX4", NULL);
        /*
         * According to SPI protocol:
         * CPHA=1 trailing half clock cycle is sampling phase
         * We technically should not drive out miso
         * However, when the kernel bitbang driver is setting the clk pin,
         * it will overwrite the miso value, so we are driving out miso in
         * the sampling half clock cycle as well to workaround this issue
         */
        s->miso = !!(s->output_bits & 0x80);
        object_property_set_bool(OBJECT(s->controller_state), "gpioX5", s->miso,
                                 NULL);
    }
}

static void cs_set_level(void *opaque, int n, int level)
{
    SpiGpioState *s = SPI_GPIO(opaque);
    s->cs = !!level;

    /* relay the CS value to the CS output pin */
    qemu_set_irq(s->cs_output_pin, s->cs);

    s->miso = !!(s->output_bits & 0x80);
    object_property_set_bool(OBJECT(s->controller_state),
                             "gpioX5", s->miso, NULL);

    s->clk = !!(s->mode & SPI_CPOL);
}

static void clk_set_level(void *opaque, int n, int level)
{
    SpiGpioState *s = SPI_GPIO(opaque);

    bool cur = !!level;

    /* CS# is high/not selected, do nothing */
    if (s->cs) {
        return;
    }

    /* When the lock has not changed, do nothing */
    if (s->clk == cur) {
        return;
    }

    s->clk = cur;

    /* Leading edge */
    if (s->clk != s->CIDLE) {
        do_leading_edge(s);
    }

    /* Trailing edge */
    if (s->clk == s->CIDLE) {
        do_trailing_edge(s);
        s->clk_counter++;

        /*
         * Deliver the input to and
         * get the next output byte
         * from the SPI device
         */
        if (s->clk_counter == 8) {
            s->output_bits = ssi_transfer(s->spi, s->input_bits);
            s->clk_counter = 0;
            s->input_bits = 0;
         } else {
            s->input_bits <<= 1;
            s->output_bits <<= 1;
         }
    }
}

static void spi_gpio_realize(DeviceState *dev, Error **errp)
{
    SpiGpioState *s = SPI_GPIO(dev);

    s->spi = ssi_create_bus(dev, "spi");
    s->spi->preread = true;

    s->mode = 0;
    s->clk_counter = 0;

    s->cs = true;
    s->clk = true;

    /* Assuming the first output byte is 0 */
    s->output_bits = 0;
    s->CIDLE = !!(s->mode & SPI_CPOL);
    s->CPHA = !!(s->mode & SPI_CPHA);

    /* init the input GPIO lines */
    /* SPI_CS_in connects to the Aspeed GPIO */
    qdev_init_gpio_in_named(dev, cs_set_level, "SPI_CS_in", 1);
    qdev_init_gpio_in_named(dev, clk_set_level, "SPI_CLK", 1);

    /* init the output GPIO lines */
    /* SPI_CS_out connects to the SSI_GPIO_CS */
    qdev_init_gpio_out_named(dev, &s->cs_output_pin, "SPI_CS_out", 1);

    qdev_connect_gpio_out_named(s->controller_state, "sysbus-irq",
                                AST_GPIO_IRQ_X0_NUM, qdev_get_gpio_in_named(
                                DEVICE(s), "SPI_CS_in", 0));
    qdev_connect_gpio_out_named(s->controller_state, "sysbus-irq",
                                AST_GPIO_IRQ_X3_NUM, qdev_get_gpio_in_named(
                                DEVICE(s), "SPI_CLK", 0));
    object_property_set_bool(OBJECT(s->controller_state), "gpioX5", true, NULL);
}

static void SPI_GPIO_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = spi_gpio_realize;
}

static const TypeInfo SPI_GPIO_info = {
    .name           = TYPE_SPI_GPIO,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(SpiGpioState),
    .class_init     = SPI_GPIO_class_init,
};

static void SPI_GPIO_register_types(void)
{
    type_register_static(&SPI_GPIO_info);
}

type_init(SPI_GPIO_register_types)
