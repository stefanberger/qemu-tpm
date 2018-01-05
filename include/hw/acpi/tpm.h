/*
 * tpm.h - TPM ACPI definitions
 *
 * Copyright (C) 2014 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputinggroup.org
 *
 */
#ifndef HW_ACPI_TPM_H
#define HW_ACPI_TPM_H

#define TPM_TIS_ADDR_BASE           0xFED40000
#define TPM_TIS_ADDR_SIZE           0x5000

#define TPM_TIS_IRQ                 5

#define TPM_LOG_AREA_MINIMUM_SIZE   (64 * 1024)

#define TPM_TCPA_ACPI_CLASS_CLIENT  0
#define TPM_TCPA_ACPI_CLASS_SERVER  1

#define TPM2_ACPI_CLASS_CLIENT      0
#define TPM2_ACPI_CLASS_SERVER      1

#define TPM2_START_METHOD_MMIO      6

/*
 * Physical Presence Interface -- shared with the BIOS
 */
#define TCG_MAGIC 0x41504354

struct tpm_ppi {
    uint32_t sign;           // TCG_MAGIC
    uint16_t size;           // number of subsequent bytes for ACPI to access
    uint8_t  opcode;         // set by ACPI
    uint8_t  failure;        // set by BIOS (0 = success)
    uint8_t  recent_opcode;  // set by BIOS
    uint32_t response;       // set by BIOS
    uint8_t  next_step;      // BIOS only
} QEMU_PACKED;

#define TPM_PPI_STRUCT_SIZE  14

#endif /* HW_ACPI_TPM_H */
