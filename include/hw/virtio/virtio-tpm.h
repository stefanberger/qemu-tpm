#ifndef QEMU_VIRTIO_TPM_H
#define QEMU_VIRTIO_TPM_H

#include "hw/virtio/virtio.h"
#include "system/tpm.h"
#include "system/tpm_backend.h"
#include "hw/acpi/tpm_ppi.h"

#define TYPE_VIRTIO_TPM        "virtio-tpm-device"

struct TPMVirtioState {
    VirtIODevice parent_obj;

    TPMBackend *tpmbe;
    TPMBackendCmd cmd;

    TPMVersion tpm_version;
    size_t be_buffer_size;

    unsigned char *buffer; /* backup buffer; only used before migration */
    uint32_t numbytes;     /* number of bytes to deliver on resume */

    VirtQueue *vq;

    bool ppi_enabled;
    TPMPPI ppi;
};
typedef struct TPMVirtioState TPMVirtioState;

#endif /* QEMU_VIRTIO_TPM_H */
