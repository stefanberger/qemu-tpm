/*
 * virtio ccw TPM implementation
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Stefan Berger <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-ccw.h"
#include "hw/virtio/virtio-tpm.h"

#define TYPE_VIRTIO_TPM_CCW "virtio-tpm-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOTPMCcw, VIRTIO_TPM_CCW)

struct VirtIOTPMCcw {
    VirtioCcwDevice parent_obj;
    TPMVirtioState vdev;
};

static void virtio_ccw_tpm_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtIOTPMCcw *dev = VIRTIO_TPM_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void virtio_ccw_tpm_instance_init(Object *obj)
{
    VirtIOTPMCcw *dev = VIRTIO_TPM_CCW(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_TPM);
}

static void virtio_ccw_tpm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_tpm_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo virtio_ccw_tpm = {
    .name          = TYPE_VIRTIO_TPM_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtIOTPMCcw),
    .instance_init = virtio_ccw_tpm_instance_init,
    .class_init    = virtio_ccw_tpm_class_init,
};

static void virtio_ccw_tpm_register(void)
{
    type_register_static(&virtio_ccw_tpm);
}

type_init(virtio_ccw_tpm_register)
