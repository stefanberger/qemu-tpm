/*
 * Aspeed i2c bus interface to reading and writing to i2c device registers
 *
 * Copyright (c) 2023 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qtest_aspeed.h"

#include "hw/i2c/aspeed_i2c.h"
#include "libqtest-single.h"

#define A_I2CD_M_STOP_CMD	BIT(5)
#define A_I2CD_M_RX_CMD		BIT(3)
#define A_I2CD_M_TX_CMD		BIT(1)
#define A_I2CD_M_START_CMD	BIT(0)

#define A_I2CD_MASTER_EN	BIT(0)

static void aspeed_i2c_startup(uint32_t baseaddr, uint8_t slave_addr,
                               uint8_t reg)
{
    uint32_t val;
    static int once = 0;

    if (!once) {
        /* enable master */
       writel(baseaddr + A_I2CC_FUN_CTRL, 0);
       val = readl(baseaddr + A_I2CC_FUN_CTRL) | A_I2CD_MASTER_EN;
       writel(baseaddr + A_I2CC_FUN_CTRL, val);
       once = 1;
    }

    /* select device */
    writel(baseaddr + A_I2CD_BYTE_BUF, slave_addr << 1);
    writel(baseaddr + A_I2CD_CMD,
           A_I2CD_M_START_CMD | A_I2CD_M_RX_CMD);

    /* select the register to write to */
    writel(baseaddr + A_I2CD_BYTE_BUF, reg);
    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_TX_CMD);
}

uint32_t aspeed_i2c_readl(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg)
{
    uint32_t res = 0;
    unsigned int i;
    uint32_t value;

    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    // read response
    for (i = 0; i < sizeof(value); i++) {
        writel(baseaddr+ A_I2CD_CMD, A_I2CD_M_RX_CMD);
        value = readl(baseaddr + A_I2CD_BYTE_BUF) >> 8;
        res = res << 8 | (value & 0xff);
    }

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);

    return be32_to_cpu(res);
}

uint16_t aspeed_i2c_readw(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg)
{
    uint16_t res;

    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    // read response
    writel(baseaddr+ A_I2CD_CMD, A_I2CD_M_RX_CMD);
    res = readl(baseaddr + A_I2CD_BYTE_BUF) >> 8;

    writel(baseaddr+ A_I2CD_CMD, A_I2CD_M_RX_CMD);
    res = res << 8 | ((readl(baseaddr + A_I2CD_BYTE_BUF) >> 8) & 0xff);

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);

    return res;
}

uint8_t aspeed_i2c_readb(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg)
{
    uint8_t res;

    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    // read response
    writel(baseaddr+ A_I2CD_CMD, A_I2CD_M_RX_CMD);
    res = readl(baseaddr + A_I2CD_BYTE_BUF) >> 8;

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);

    return res;
}

void aspeed_i2c_writeb(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint8_t value)
{
    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    /* write the byte */
    writel(baseaddr + A_I2CD_BYTE_BUF, value);
    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_TX_CMD);

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);
}

void aspeed_i2c_writel(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint32_t value)
{
    unsigned int i;

    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    /* write the bytes */
    for (i = 0; i < sizeof(value); i++) {
        writel(baseaddr + A_I2CD_BYTE_BUF, value & 0xff);
        value >>= 8;
        writel(baseaddr + A_I2CD_CMD, A_I2CD_M_TX_CMD);
    }

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);
}

void aspeed_i2c_writew(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint16_t value)
{
    unsigned int i;

    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    /* write the bytes */
    for (i = 0; i < sizeof(value); i++) {
        writel(baseaddr + A_I2CD_BYTE_BUF, value & 0xff);
        value >>= 8;
        writel(baseaddr + A_I2CD_CMD, A_I2CD_M_TX_CMD);
    }

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);
}
