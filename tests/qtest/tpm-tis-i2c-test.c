/*
 * QTest testcase for I2C TPM TIS
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
#include <glib/gstdio.h>

#include "libqtest-single.h"
#include "hw/acpi/tpm.h"
#include "hw/pci/pci_ids.h"
#include "qtest_aspeed.h"
#include "tpm-emu.h"

#define DEBUG_TIS_TEST 0

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TIS_TEST) { \
        printf(fmt, ## __VA_ARGS__); \
    } \
} while (0)

#define DPRINTF_ACCESS \
    DPRINTF("%s: %d: locty=%d l=%d access=0x%02x pending_request_flag=0x%x\n", \
            __func__, __LINE__, locty, l, access, pending_request_flag)

#define DPRINTF_STS \
    DPRINTF("%s: %d: sts = 0x%08x\n", __func__, __LINE__, sts)

#define I2C_SLAVE_ADDR		0x2e
#define I2C_DEV_BUS_NUM           10

static const uint8_t TPM_CMD[12] =
    "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00";

// FIXME: remove defines once in public header file

/* TPM TIS I2C registers */
#define TPM_TIS_I2C_REG_LOC_SEL          0x00
#define TPM_TIS_I2C_REG_ACCESS           0x04
#define TPM_TIS_I2C_REG_INT_ENABLE       0x08
#define TPM_TIS_I2C_REG_INT_CAPABILITY   0x14
#define TPM_TIS_I2C_REG_STS              0x18
#define TPM_TIS_I2C_REG_DATA_FIFO        0x24
#define TPM_TIS_I2C_REG_INTF_CAPABILITY  0x30
#define TPM_TIS_I2C_REG_I2C_DEV_ADDRESS  0x38
#define TPM_TIS_I2C_REG_DATA_CSUM_ENABLE 0x40
#define TPM_TIS_I2C_REG_DATA_CSUM_GET    0x44
#define TPM_TIS_I2C_REG_DID_VID          0x48
#define TPM_TIS_I2C_REG_RID              0x4c
#define TPM_TIS_I2C_REG_UNKNOWN          0xff

/* I2C specific interface capabilities */
#define TPM_I2C_CAP_INTERFACE_TYPE     (0x2 << 0)       /* FIFO interface */
#define TPM_I2C_CAP_INTERFACE_VER      (0x0 << 4)       /* TCG I2C intf 1.0 */
#define TPM_I2C_CAP_TPM2_FAMILY        (0x1 << 7)       /* TPM 2.0 family. */
#define TPM_I2C_CAP_DEV_ADDR_CHANGE    (0x0 << 27)      /* No dev addr chng */
#define TPM_I2C_CAP_BURST_COUNT_STATIC (0x1 << 29)      /* Burst count static */
#define TPM_I2C_CAP_LOCALITY_CAP       (0x1 << 25)      /* 0-5 locality */
#define TPM_I2C_CAP_BUS_SPEED          (3   << 21)      /* std and fast mode */

/* TPM_STS mask for read bits 31:26 must be zero */
#define TPM_I2C_STS_READ_MASK          0x03ffffff

uint64_t tpm_tis_base_addr = TPM_TIS_ADDR_BASE;
uint32_t aspeed_dev_addr = 0X1e78a000 + 0x80 + I2C_DEV_BUS_NUM * 0x80;

static void tpm_tis_i2c_test_check_localities(const void *data)
{
    uint8_t slave_addr = I2C_SLAVE_ADDR;
    uint8_t locty;
    uint8_t access;
    uint32_t capability, i2c_cap;
    uint32_t didvid;
    uint32_t rid;
    uint32_t val;

    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES; locty++) {
        /* select locality */

        if (0) { // FIXME: not working, yet
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_LOC_SEL, locty);
            val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                   TPM_TIS_I2C_REG_LOC_SEL);
            g_assert_cmpint(locty, ==, val);
        }

        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        capability = aspeed_i2c_readl(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_INTF_CAPABILITY);
        i2c_cap = (TPM_I2C_CAP_INTERFACE_TYPE |
                   TPM_I2C_CAP_INTERFACE_VER  |
                   TPM_I2C_CAP_TPM2_FAMILY    |
                   TPM_I2C_CAP_LOCALITY_CAP   |
                   TPM_I2C_CAP_BUS_SPEED      |
                   TPM_I2C_CAP_DEV_ADDR_CHANGE);
        g_assert_cmpint(capability, ==, i2c_cap);

        didvid = aspeed_i2c_readl(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_DID_VID);
        g_assert_cmpint(didvid, ==, (1 << 16) | PCI_VENDOR_ID_IBM);

        rid = aspeed_i2c_readl(aspeed_dev_addr, slave_addr,
                               TPM_TIS_I2C_REG_RID);
        g_assert_cmpint(rid, !=, 0);
        g_assert_cmpint(rid, !=, 0xffffffff);
    }
}

static void tpm_tis_i2c_test_basic(const void *data)
{
    uint8_t slave_addr = I2C_SLAVE_ADDR;
    uint8_t val, access;

    /* select locality 0 */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr, TPM_TIS_I2C_REG_LOC_SEL, 0);

    /* request use of locality 0 */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);
    access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    /* enable all interrupts */
    aspeed_i2c_writel(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_INT_ENABLE, 0x80000007);
    if (0) { // FIXME: not working yet
        val = aspeed_i2c_readl(aspeed_dev_addr, slave_addr, 0x08);
        g_assert_cmpint(val, ==, 0x80000007);
    }

    /* release access */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_ACCESS,
                      TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
}

static void tpm_tis_i2c_test_check_access_reg(const void *data)
{
    uint8_t slave_addr = I2C_SLAVE_ADDR;
    uint8_t locty;
    uint8_t access;
    uint8_t val;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        if (0) { // FIXME: not working, yet
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_LOC_SEL, locty);
            val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                   TPM_TIS_I2C_REG_LOC_SEL);
            g_assert_cmpint(locty, ==, val);
        }

        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        if (0) // FIXME: not working, yet
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                          TPM_TIS_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);

        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* release access */
        aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                          TPM_TIS_I2C_REG_ACCESS,
                          TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    }
}

/*
 * Test case for seizing access by a higher number locality
 */
static void tpm_tis_i2c_test_check_access_reg_seize(const void *data)
{
    uint8_t slave_addr = I2C_SLAVE_ADDR;
    int locty, l;
    uint8_t access, val;
    uint8_t pending_request_flag;

    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        if (0) { // FIXME: not working, yet
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_LOC_SEL, locty);
            val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                   TPM_TIS_I2C_REG_LOC_SEL);
            g_assert_cmpint(locty, ==, val);
        }

        pending_request_flag = 0;

        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                          TPM_TIS_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);
        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* lower localities cannot seize access */
        for (l = 0; l < locty; l++) {
            if (0) { // FIXME: not working, yet
                aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_LOC_SEL, l);
                val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                       TPM_TIS_I2C_REG_LOC_SEL);
                g_assert_cmpint(l, ==, val);
            }
            /* lower locality is not active */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to request use from 'l' */
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS,
                              TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible;
             * we must see REQUEST_USE and possibly PENDING_REQUEST
             */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_REQUEST_USE |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'locty' must be unchanged;
             * we must see PENDING_REQUEST
             */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            if (0) //
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                            TPM_TIS_ACCESS_PENDING_REQUEST |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS, TPM_TIS_ACCESS_SEIZE);
            /* seize from 'l' was not possible */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_REQUEST_USE |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* locality 'locty' must be unchanged */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                            TPM_TIS_ACCESS_PENDING_REQUEST |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * on the next loop we will have a PENDING_REQUEST flag
             * set for locality 'l'
             */
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }

        /*
         * higher localities can 'seize' access but not 'request use';
         * note: this will activate first l+1, then l+2 etc.
         */
        for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            if (0) { // FIXME: not working, yet
                aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_LOC_SEL, l);
                val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                       TPM_TIS_I2C_REG_LOC_SEL);
                g_assert_cmpint(l, ==, val);
            }
            /* try to 'request use' from 'l' */
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS,
                              TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible; we should see
             * REQUEST_USE and may see PENDING_REQUEST
             */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0) // FIXME: not working, yet
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_REQUEST_USE |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'l-1' must be unchanged; we should always
             * see PENDING_REQUEST from 'l' requesting access
             */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                            TPM_TIS_ACCESS_PENDING_REQUEST |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS, TPM_TIS_ACCESS_SEIZE);

            /* seize from 'l' was possible */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* l - 1 should show that it has BEEN_SEIZED */
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_BEEN_SEIZED |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* clear the BEEN_SEIZED flag and make sure it's gone */
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS,
                              TPM_TIS_ACCESS_BEEN_SEIZED);

            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            if (0) // FIXME
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }

        /*
         * PENDING_REQUEST will not be set if locty = 0 since all localities
         * were active; in case of locty = 1, locality 0 will be active
         * but no PENDING_REQUEST anywhere
         */
        if (locty <= 1) {
            pending_request_flag = 0;
        }

        /* release access from l - 1; this activates locty - 1 */
        l--;

        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        DPRINTF_ACCESS;

        DPRINTF("%s: %d: relinquishing control on l = %d\n",
                __func__, __LINE__, l);
        aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                          TPM_TIS_I2C_REG_ACCESS,
                          TPM_TIS_ACCESS_ACTIVE_LOCALITY);

        access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_ACCESS);
        DPRINTF_ACCESS;
        if (0) // FIXME
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = locty - 1; l >= 0; l--) {
            if (0) { // FIXME: not working, yet
                aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_LOC_SEL, l);
                val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                       TPM_TIS_I2C_REG_LOC_SEL);
                g_assert_cmpint(l, ==, val);
            }
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            if (0)
                g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                            TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                            pending_request_flag |
                                            TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* release this locality */
            aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS,
                              TPM_TIS_ACCESS_ACTIVE_LOCALITY);

            if (l == 1) {
                pending_request_flag = 0;
            }
        }

        /* no locality may be active now */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            if (0) { // FIXME: not working, yet
                aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                                  TPM_TIS_I2C_REG_LOC_SEL, l);
                val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                       TPM_TIS_I2C_REG_LOC_SEL);
                g_assert_cmpint(l, ==, val);
            }
            access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
    }
}


/*
 * Test case for transmitting packets
 */
static void tpm_tis_i2c_test_check_transmit(const void *data)
{
    const TPMTestState *s = data;
    uint8_t slave_addr = I2C_SLAVE_ADDR;
    uint8_t access, val;
    uint32_t sts;
    uint16_t bcount, csum;
    size_t i;

    /* select locality 0 */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr, TPM_TIS_I2C_REG_LOC_SEL, 0);

    /* enable csum */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_DATA_CSUM_ENABLE, 0x1);
    /* check csum enable register has bit 0 set */
    val = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                           TPM_TIS_I2C_REG_DATA_CSUM_ENABLE);
    if (0) // FIXME: Not working, yet
        g_assert_cmpint(val, ==, 1);

    /* request use of locality 0 */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);
    access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    sts = aspeed_i2c_readl(aspeed_dev_addr, slave_addr, TPM_TIS_I2C_REG_STS);
    DPRINTF_STS;

    g_assert_cmpint(sts & 0xff, ==, 0);

    bcount = (sts >> 8) & 0xffff;
    g_assert_cmpint(bcount, >=, 128);

    aspeed_i2c_writel(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_STS, TPM_TIS_STS_COMMAND_READY);
    sts = aspeed_i2c_readl(aspeed_dev_addr, slave_addr, TPM_TIS_I2C_REG_STS);
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_COMMAND_READY);

    /* transmit command */
    for (i = 0; i < sizeof(TPM_CMD); i++) {
        aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                          TPM_TIS_I2C_REG_DATA_FIFO, TPM_CMD[i]);
        sts = aspeed_i2c_readl(aspeed_dev_addr, slave_addr,
                               TPM_TIS_I2C_REG_STS);
        DPRINTF_STS;
        if (i < sizeof(TPM_CMD) - 1) {
            g_assert_cmpint(sts & 0xff, ==,
                            TPM_TIS_STS_EXPECT | TPM_TIS_STS_VALID);
        } else {
            g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_VALID);
        }
        g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
    }
    /* read the checksum */
    csum = aspeed_i2c_readw(aspeed_dev_addr, slave_addr,
                            TPM_TIS_I2C_REG_DATA_CSUM_ENABLE);
    fprintf(stderr, ">>> csum=0x%08x\n", csum);
    if (0) // FIXME: Not working, yet
         g_assert_cmpint(csum, ==, 0x6733);

    /* start processing */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_STS, TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = aspeed_i2c_readl(aspeed_dev_addr, slave_addr,
                               TPM_TIS_I2C_REG_STS);
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    sts = aspeed_i2c_readl(aspeed_dev_addr, slave_addr,
                           TPM_TIS_I2C_REG_STS);
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, == ,
                    TPM_TIS_STS_VALID | TPM_TIS_STS_DATA_AVAILABLE);
    bcount = (sts >> 8) & 0xffff;

    /* read response */
    uint8_t tpm_msg[sizeof(struct tpm_hdr)];
    g_assert_cmpint(sizeof(tpm_msg), ==, bcount);

    for (i = 0; i < sizeof(tpm_msg); i++) {
        tpm_msg[i] = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                                      TPM_TIS_I2C_REG_DATA_FIFO);
        sts = aspeed_i2c_readl(aspeed_dev_addr, slave_addr,
                               TPM_TIS_I2C_REG_STS);
        DPRINTF_STS;
        if (sts & TPM_TIS_STS_DATA_AVAILABLE) {
            fprintf(stderr, "sts = 0x%x\n", sts);
            if (0) // FIXME: Not working, yet
                g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
        }
    }
    g_assert_cmpmem(tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    /* relinquish use of locality 0 */
    aspeed_i2c_writeb(aspeed_dev_addr, slave_addr,
                      TPM_TIS_I2C_REG_ACCESS, TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = aspeed_i2c_readb(aspeed_dev_addr, slave_addr,
                              TPM_TIS_I2C_REG_ACCESS);
}

int main(int argc, char **argv)
{
    int ret;
    char *args, *tmp_path = g_dir_make_tmp("qemu-tpm-tis-i2c-test.XXXXXX", NULL);
    GThread *thread;
    TPMTestState test;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);
    test.data_cond_signal = false;
    test.tpm_version = TPM_VERSION_2_0;

    thread = g_thread_new(NULL, tpm_emu_ctrl_thread, &test);
    tpm_emu_test_wait_cond(&test);

    args = g_strdup_printf(
        "-machine rainier-bmc -accel tcg --trace tpm_tis_* "
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=tpm0,chardev=chr "
        "-device tpm-tis-i2c,tpmdev=tpm0,bus=aspeed.i2c.bus.%d,address=0x%x",
        test.addr->u.q_unix.path,
        I2C_DEV_BUS_NUM,
        I2C_SLAVE_ADDR);
    qtest_start(args);

    if (1)
        qtest_add_data_func("/tpm-tis-i2c/test_check_localities", &test,
                            tpm_tis_i2c_test_check_localities);

    if (1)
        qtest_add_data_func("/tpm-tis-i2c/test_basic", &test,
                            tpm_tis_i2c_test_basic);

    if (1)
        qtest_add_data_func("/tpm-tis-i2c/check_access_reg", &test,
                            tpm_tis_i2c_test_check_access_reg);

    if (1)
        qtest_add_data_func("/tpm-tis-i2c/check_access_reg_seize", &test,
                            tpm_tis_i2c_test_check_access_reg_seize);

    if (1)
        qtest_add_data_func("/tpm-tis-i2c/test_check_transmit", &test,
                            tpm_tis_i2c_test_check_transmit);

    ret = g_test_run();

    qtest_end();

    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(args);
    return ret;
}
