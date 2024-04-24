/*
 * tpm_crb.c - QEMU's TPM CRB interface emulator
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * tpm_crb is a device for TPM 2.0 Command Response Buffer (CRB) Interface
 * as defined in TCG PC Client Platform TPM Profile (PTP) Specification
 * Family “2.0” Level 00 Revision 01.03 v22
 */

#include "qemu/osdep.h"

#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_ids.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "sysemu/tpm_backend.h"
#include "sysemu/tpm_util.h"
#include "tpm_prop.h"
#include "tpm_ppi.h"
#include "trace.h"
#include "qom/object.h"
#include "tpm_crb.h"

/*
 * Use this macro to set values in the registers (saved_regs) and MMIO-mapped
 * registers.
 */
#define ARRAY_FIELD_DP32_ROMD_LE(saved_regs, reg, field, val, regs)	\
    do {								\
        ARRAY_FIELD_DP32(saved_regs, reg, field, val);			\
        regs[R_##reg] = cpu_to_le32(saved_regs[R_##reg]);		\
    } while (0)

#define LOAD_REG32_ROMD_LE(saved_regs, reg, val, regs)			\
    do {								\
        saved_regs[R_##reg] = val;					\
        regs[R_##reg] = cpu_to_le32(val);				\
    } while (0)

static uint8_t tpm_crb_get_active_locty(TPMCRBState *s, uint32_t *saved_regs)
{
    if (!ARRAY_FIELD_EX32(saved_regs, CRB_LOC_STATE, locAssigned)) {
        return TPM_CRB_NO_LOCALITY;
    }
    return ARRAY_FIELD_EX32(saved_regs, CRB_LOC_STATE, activeLocality);
}

static void tpm_crb_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    TPMCRBState *s = opaque;
    uint8_t locty =  addr >> 12;
    uint32_t *regs, *saved_regs;
    unsigned i;
    void *mem;

    saved_regs = s->saved_regs;

    trace_tpm_crb_mmio_write(addr, size, val);
    regs = memory_region_get_ram_ptr(&s->mmio);
    mem = &regs[R_CRB_DATA_BUFFER];
    assert(regs);

    /* receive TPM command bytes in DATA_BUFFER */
    if (addr >= A_CRB_DATA_BUFFER) {
        assert(addr + size <= TPM_CRB_ADDR_SIZE);
        assert(size <= sizeof(val));
        for (i = 0; i < size; i++) {
            *(char *)(mem + addr - A_CRB_DATA_BUFFER + i) = val;
            val >>= 8;
        }
        memory_region_set_dirty(&s->mmio, addr, size);
        return;
    }

    /* otherwise we are doing MMIO writes */
    switch (addr) {
    case A_CRB_CTRL_REQ:
        switch (val) {
        case CRB_CTRL_REQ_CMD_READY:
            ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_CTRL_STS,
                                     tpmIdle, 0, regs);
            break;
        case CRB_CTRL_REQ_GO_IDLE:
            ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_CTRL_STS,
                                     tpmIdle, 1, regs);
            break;
        }
        break;
    case A_CRB_CTRL_CANCEL:
        if (val == CRB_CANCEL_INVOKE &&
            saved_regs[R_CRB_CTRL_START] & CRB_START_INVOKE) {
            tpm_backend_cancel_cmd(s->tpmbe);
        }
        break;
    case A_CRB_CTRL_START:
        if (val == CRB_START_INVOKE &&
            !(saved_regs[R_CRB_CTRL_START] & CRB_START_INVOKE) &&
            tpm_crb_get_active_locty(s, saved_regs) == locty) {

            saved_regs[R_CRB_CTRL_START] |= CRB_START_INVOKE;
            regs[R_CRB_CTRL_START] = cpu_to_le32(saved_regs[R_CRB_CTRL_START]);
            s->cmd = (TPMBackendCmd) {
                .in = mem,
                .in_len = MIN(tpm_cmd_get_size(mem), s->be_buffer_size),
                .out = mem,
                .out_len = s->be_buffer_size,
            };

            tpm_backend_deliver_request(s->tpmbe, &s->cmd);
        }
        break;
    case A_CRB_LOC_CTRL:
        switch (val) {
        case CRB_LOC_CTRL_RESET_ESTABLISHMENT_BIT:
            /* not loc 3 or 4 */
            break;
        case CRB_LOC_CTRL_RELINQUISH:
            ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_LOC_STATE,
                                     locAssigned, 0, regs);
            ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_LOC_STS,
                                     Granted, 0, regs);
            break;
        case CRB_LOC_CTRL_REQUEST_ACCESS:
            ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_LOC_STS,
                                     Granted, 1, regs);
            ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_LOC_STS,
                                     beenSeized, 0, regs);
            ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_LOC_STATE,
                                     locAssigned, 1, regs);
            break;
        }
        break;
    }

    memory_region_set_dirty(&s->mmio, 0, A_CRB_DATA_BUFFER);
}

const MemoryRegionOps tpm_crb_memory_ops = {
    .write = tpm_crb_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

void tpm_crb_request_completed(TPMCRBState *s, int ret)
{
    uint32_t *regs = memory_region_get_ram_ptr(&s->mmio);
    uint32_t *saved_regs = s->saved_regs;

    assert(regs);
    saved_regs[R_CRB_CTRL_START] &= ~CRB_START_INVOKE;
    regs[R_CRB_CTRL_START] = cpu_to_le32(saved_regs[R_CRB_CTRL_START]);
    if (ret != 0) {
        ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_CTRL_STS,
                                 tpmSts, 1, regs); /* fatal error */
    }

    memory_region_set_dirty(&s->mmio, 0, TPM_CRB_ADDR_SIZE);
}

enum TPMVersion tpm_crb_get_version(TPMCRBState *s)
{
    return tpm_backend_get_tpm_version(s->tpmbe);
}

int tpm_crb_pre_save(TPMCRBState *s)
{
    tpm_backend_finish_sync(s->tpmbe);

    return 0;
}

void tpm_crb_reset(TPMCRBState *s, uint64_t baseaddr)
{
    uint32_t *saved_regs = s->saved_regs;
    uint32_t *regs = memory_region_get_ram_ptr(&s->mmio);

    assert(regs);
    if (s->ppi_enabled) {
        tpm_ppi_reset(&s->ppi);
    }
    tpm_backend_reset(s->tpmbe);

    memset(regs, 0, TPM_CRB_ADDR_SIZE);
    memset(s->saved_regs, 0, sizeof(s->saved_regs));

    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_LOC_STATE,
                             tpmRegValidSts, 1, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_LOC_STATE,
                             tpmEstablished, 1, regs);

    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_CTRL_STS,
                             tpmIdle, 1, regs);

    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             InterfaceType, CRB_INTF_TYPE_CRB_ACTIVE, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             InterfaceVersion, CRB_INTF_VERSION_CRB, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             CapLocality, CRB_INTF_CAP_LOCALITY_0_ONLY, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             CapCRBIdleBypass, CRB_INTF_CAP_IDLE_FAST, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             CapDataXferSizeSupport, CRB_INTF_CAP_XFER_SIZE_64,
                             regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             CapFIFO, CRB_INTF_CAP_FIFO_NOT_SUPPORTED, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             CapCRB, CRB_INTF_CAP_CRB_SUPPORTED, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             InterfaceSelector, CRB_INTF_IF_SELECTOR_CRB, regs);
    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID,
                             RID, 0b0000, regs);

    ARRAY_FIELD_DP32_ROMD_LE(saved_regs, CRB_INTF_ID2,
                             VID, PCI_VENDOR_ID_IBM, regs);

    baseaddr += A_CRB_DATA_BUFFER;
    LOAD_REG32_ROMD_LE(saved_regs, CRB_CTRL_CMD_SIZE, CRB_CTRL_CMD_SIZE, regs);
    LOAD_REG32_ROMD_LE(saved_regs, CRB_CTRL_CMD_LADDR, (uint32_t)baseaddr, regs);
    LOAD_REG32_ROMD_LE(saved_regs, CRB_CTRL_CMD_HADDR,
                       (uint32_t)(baseaddr >> 32), regs);
    LOAD_REG32_ROMD_LE(saved_regs, CRB_CTRL_RSP_SIZE, CRB_CTRL_CMD_SIZE, regs);
    LOAD_REG32_ROMD_LE(saved_regs, CRB_CTRL_RSP_LADDR, (uint32_t)baseaddr, regs);
    LOAD_REG32_ROMD_LE(saved_regs, CRB_CTRL_RSP_HADDR,
                       (uint32_t)(baseaddr >> 32), regs);

    s->be_buffer_size = MIN(tpm_backend_get_buffer_size(s->tpmbe),
                            CRB_CTRL_CMD_SIZE);

    if (tpm_backend_startup_tpm(s->tpmbe, s->be_buffer_size) < 0) {
        exit(1);
    }

    memory_region_rom_device_set_romd(&s->mmio, true);
    memory_region_set_dirty(&s->mmio, 0, TPM_CRB_ADDR_SIZE);
}

void tpm_crb_init_memory(Object *obj, TPMCRBState *s, Error **errp)
{
    /*
     * To be able to map the romd device's read-only memory area it must be at
     * least the size of a page of the host. Pages can be 4k, 16k or 64k. We
     * choose 16k, which enables also migration to hosts with 16k pages.
     */
    uint64_t tpm_crb_addr_size = 16 * 1024;

    memory_region_init_rom_device_nomigrate(&s->mmio, obj, &tpm_crb_memory_ops,
        s, "tpm-crb-mem", tpm_crb_addr_size, errp);
    if (s->ppi_enabled) {
        tpm_ppi_init_memory(&s->ppi, obj);
    }
}

void tpm_crb_mem_save(TPMCRBState *s, void *saved_cmdmem)
{
    uint32_t *regs = memory_region_get_ram_ptr(&s->mmio);

    memcpy(saved_cmdmem, &regs[R_CRB_DATA_BUFFER], CRB_CTRL_CMD_SIZE);
}

void tpm_crb_mem_load(TPMCRBState *s, const void *saved_cmdmem)
{
    uint32_t *regs = memory_region_get_ram_ptr(&s->mmio);

    memcpy(&regs[R_CRB_DATA_BUFFER], saved_cmdmem, CRB_CTRL_CMD_SIZE);
}

void tpm_crb_build_aml(TPMIf *ti, Aml *scope, uint32_t baseaddr, uint32_t size,
                       bool build_ppi)
{
    Aml *dev, *crs;

    dev = aml_device("TPM");
    aml_append(dev, aml_name_decl("_HID", aml_string("MSFT0101")));
    aml_append(dev, aml_name_decl("_STR", aml_string("TPM 2.0 Device")));
    aml_append(dev, aml_name_decl("_UID", aml_int(1)));
    aml_append(dev, aml_name_decl("_STA", aml_int(0xF)));
    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(baseaddr, size, AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    if (build_ppi) {
        tpm_build_ppi_acpi(ti, dev);
    }
    aml_append(scope, dev);
}
