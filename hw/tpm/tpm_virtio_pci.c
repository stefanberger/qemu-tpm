/*
 * tpm_virtio.c - QEMU's TPM Virtio interface
 *
 * Copyright (c) 2025 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.ibm.com>
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
#include "hw/pci/pci_ids.h"
#include "migration/vmstate.h"
#include "system/reset.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-tpm.h"

#define  TYPE_VIRTIO_TPM_PCI "virtio-tpm-pci-base"

struct TPMVirtioPCI {
    VirtIOPCIProxy parent_obj;
    TPMVirtioState vdev;
};
typedef struct TPMVirtioPCI TPMVirtioPCI;

DECLARE_INSTANCE_CHECKER(TPMVirtioPCI, TPM_VIRTIO_PCI,
                         TYPE_VIRTIO_TPM_PCI);

static void tpm_virtio_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    TPMVirtioPCI *dev = TPM_VIRTIO_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void tpm_virtio_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = tpm_virtio_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_TPM;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_CRYPT_OTHER;
}

static void tpm_virtio_pci_instance_init(Object *obj)
{
    TPMVirtioPCI *dev = TPM_VIRTIO_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_TPM);
}

static const VirtioPCIDeviceTypeInfo tpm_virtio_pci_info = {
    .base_name = TYPE_VIRTIO_TPM_PCI,
    .generic_name = "virtio-tpm-pci",
    .transitional_name = "virtio-tpm-pci-transitional",
    .non_transitional_name = "virtio-tpm-pci-non-transitional",
    .instance_size = sizeof(TPMVirtioPCI),
    .instance_init = tpm_virtio_pci_instance_init,
    .class_init = tpm_virtio_pci_class_init,
};

static void tpm_virtio_register(void)
{
    virtio_pci_types_register(&tpm_virtio_pci_info);
}

type_init(tpm_virtio_register)
