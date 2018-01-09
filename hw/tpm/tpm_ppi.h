/*
 * TPM Physical Presence Interface
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TPM_TPM_PPI_H
#define TPM_TPM_PPI_H

#define TPM_PPI_MEMORY_SIZE  0x100
#define TPM_PPI_ADDR_BASE    0xffff0000

typedef struct TPMPPI {
    MemoryRegion mmio;

    uint8_t ram[TPM_PPI_MEMORY_SIZE];
} TPMPPI;

void tpm_ppi_init_io(TPMPPI *tpmppi, Object *obj);

#endif /* TPM_TPM_PPI_H */
