/*
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#ifndef SPI_GPIO_H
#define SPI_GPIO_H

#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "hw/gpio/aspeed_gpio.h"

#define TYPE_SPI_GPIO "spi_gpio"
OBJECT_DECLARE_SIMPLE_TYPE(SpiGpioState, SPI_GPIO);

/* ASPEED GPIO propname values */
#define AST_GPIO_IRQ_X0_NUM 185
#define AST_GPIO_IRQ_X3_NUM 188

struct SpiGpioState {
    SysBusDevice parent;
    SSIBus *spi;
    DeviceState *controller_state;

    int mode;
    int clk_counter;

    bool CIDLE, CPHA;
    uint32_t output_bits;
    uint32_t input_bits;

    bool clk, cs, miso;
    qemu_irq cs_output_pin;
};

#endif /* SPI_GPIO_H */
