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
#include CONFIG_DEVICES /* CONFIG_ACPI */

#include "qemu/module.h"
#include "hw/pci/pci_ids.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/tpm_util.h"
#include "system/address-spaces.h"
#include "tpm_prop.h"
#include "qom/object.h"
#include "hw/acpi/tpm.h"
#include "hw/acpi/tpm_ppi.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-tpm.h"
#include "standard-headers/linux/virtio_tpm.h"
#include "standard-headers/linux/virtio_ids.h"

DECLARE_INSTANCE_CHECKER(TPMVirtioState, TPM_VIRTIO,
                         TYPE_VIRTIO_TPM);

/* Check for and respond to an invalid request */
static bool tpm_virtio_check_request(VirtQueue *vq, VirtQueueElement *elem,
                                     uint8_t *locty)
{
    struct virtio_tpm_cmd_result *result = NULL;
    struct virtio_tpm_cmd_header *hdr;

    if (elem->out_num != 2 ||
        elem->in_num != 2 ||
        elem->in_sg[1].iov_len < sizeof(struct virtio_tpm_cmd_result)) {
        goto bad_input;
    }

    result = elem->in_sg[1].iov_base;
    if (elem->out_sg[0].iov_len < sizeof(struct virtio_tpm_cmd_header) ||
        elem->out_sg[1].iov_len < 10 ||
        elem->in_sg[0].iov_len < 10)
    {
        result->status = VIRTIO_TPM_CMD_RESULT_BAD_INPUT;
        goto bad_input;
    }

    hdr = elem->out_sg[0].iov_base;

    *locty = hdr->locty;

    return true;

bad_input:
    virtqueue_push(vq, elem, 0);
    g_free(elem);

    return false;
}

/*
 * Send a request to the TPM.
 */
static void tpm_virtio_tpm_send(VirtIODevice *vdev, VirtQueue *vq)
{
    TPMVirtioState *s = TPM_VIRTIO(vdev);
    VirtQueueElement *elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    uint8_t *tpm_cmd;
    uint8_t locty;

    if (!elem ||
        !tpm_virtio_check_request(vq, elem, &locty)) {
        return;
    }

    tpm_cmd = elem->out_sg[1].iov_base;

    tpm_util_show_buffer(tpm_cmd, elem->out_sg[1].iov_len, "To TPM");

    s->cmd = (TPMBackendCmd) {
        .locty = locty,
        .in = tpm_cmd,
        .in_len = MIN(tpm_cmd_get_size(tpm_cmd),
                      elem->out_sg[1].iov_len),
        .out = elem->in_sg[0].iov_base,
        .out_len = elem->in_sg[0].iov_len,
    };

    tpm_backend_deliver_request(s->tpmbe, &s->cmd);

    virtqueue_unpop(vq, elem, 0);
}

/*
 * Indicate to the guest that the response was received.
 */
static void tpm_virtio_send_response_completed(TPMVirtioState *s,
                                               VirtQueue *vq,
                                               VirtQueueElement *elem,
                                               unsigned int len)
{
    struct virtio_tpm_cmd_result *result;

    result = elem->in_sg[1].iov_base;
    result->status = VIRTIO_TPM_CMD_RESULT_SUCCESS;

    virtqueue_push(vq, elem, len);
    g_free(elem);

    virtio_notify(&s->parent_obj, vq);
}

/*
 * Callback from the TPM to indicate that the response was received.
 */
static void tpm_virtio_request_completed(TPMIf *ti, int ret)
{
    TPMVirtioState *s = TPM_VIRTIO(ti);
    VirtQueueElement *elem;
    VirtQueue *vq;
    uint32_t len;

    vq = s->vq;
    if (!virtio_queue_ready(vq)) {
        return;
    }

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        return;
    }

    /* a max. of be_buffer_size bytes can be transported */
    len = MIN(tpm_cmd_get_size(elem->in_sg[0].iov_base), s->be_buffer_size);
    if (runstate_check(RUN_STATE_FINISH_MIGRATE)) {
        /* defer delivery of response until .post_load */
        memcpy(s->buffer, elem->in_sg[0].iov_base, len);
        s->numbytes = len;
        virtqueue_unpop(vq, elem, 0);
        return;
    }

    tpm_virtio_send_response_completed(s, vq, elem, len);
}

static int tpm_virtio_pre_save(void *opaque)
{
    TPMVirtioState *s = opaque;

    tpm_backend_finish_sync(s->tpmbe);

    return 0;
}

static int tpm_virtio_post_load(void *opaque, int version_id)
{
    TPMVirtioState *s = opaque;
    VirtQueueElement *elem;

    if (s->numbytes) {
        elem = virtqueue_pop(s->vq, sizeof(VirtQueueElement));
        memcpy(elem->in_sg[0].iov_base, s->buffer, s->numbytes);
        /* deliver the results to the VM via the queues */
        tpm_virtio_send_response_completed(s, s->vq, elem, s->numbytes);
        s->numbytes = 0;
    }

    return 0;
}

static const VMStateDescription vmstate_tpm_virtio = {
    .name = "tpm-virtio",
    .pre_save = tpm_virtio_pre_save,
    .post_load = tpm_virtio_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_UINT32(numbytes, TPMVirtioState),
        VMSTATE_VBUFFER_UINT32(buffer, TPMVirtioState, 0, NULL, numbytes),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property tpm_virtio_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", TPMVirtioState, tpmbe),
    DEFINE_PROP_BOOL("ppi", TPMVirtioState, ppi_enabled, true),
};

static void tpm_virtio_reset(void *opaque)
{
    VirtIODevice *dev = opaque;
    TPMVirtioState *s = TPM_VIRTIO(dev);

#if defined(CONFIG_ACPI) && CONFIG_ACPI
    if (s->ppi_enabled) {
        tpm_ppi_reset(&s->ppi);
    }
#endif

    tpm_backend_reset(s->tpmbe);

    s->be_buffer_size = tpm_backend_get_buffer_size(s->tpmbe);
    s->tpm_version = tpm_backend_get_tpm_version(s->tpmbe);

    if (tpm_backend_startup_tpm(s->tpmbe, s->be_buffer_size) < 0) {
        exit(1);
    }

    s->numbytes = 0;
    s->buffer = g_realloc(s->buffer, s->be_buffer_size);
}

static enum TPMVersion tpm_virtio_get_version(TPMIf *ti)
{
    TPMVirtioState *s = TPM_VIRTIO(ti);

    return tpm_backend_get_tpm_version(s->tpmbe);
}

static void tpm_virtio_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    TPMVirtioState *s = TPM_VIRTIO(dev);
    size_t config_size = sizeof(struct virtio_tpm_config);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }
    if (!s->tpmbe) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }

    /* platform reset */
    qemu_register_reset(tpm_virtio_reset, vdev);

    virtio_init(vdev, VIRTIO_ID_TPM, config_size);

    s->vq = virtio_add_queue(vdev, 4, tpm_virtio_tpm_send);

#if defined(CONFIG_ACPI) && CONFIG_ACPI
    if (s->ppi_enabled) {
        tpm_ppi_init(&s->ppi, get_system_memory(),
                     TPM_PPI_ADDR_BASE, OBJECT(s));
    }
#endif
}

static void tpm_virtio_unrealize(DeviceState *dev)
{
    TPMVirtioState *s = TPM_VIRTIO(dev);

    virtio_delete_queue(s->vq);
    g_free(s->buffer);
}

static uint64_t tpm_virtio_get_features(VirtIODevice *vdev,
                                        uint64_t features,
                                        Error **errp)
{
    virtio_add_feature(&features, VIRTIO_TPM_F_GET_BUFFERSIZE);

    return features;
}

/* Guest requested config info */
static void tpm_virtio_get_config(VirtIODevice *vdev,
                                  uint8_t *config_data)
{
    TPMVirtioState *s = TPM_VIRTIO(vdev);
    struct virtio_tpm_config *config =
        (struct virtio_tpm_config *)config_data;

    config->buffersize = s->be_buffer_size;
    config->tpm_version = s->tpm_version;
}

static void tpm_virtio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    device_class_set_props(dc, tpm_virtio_properties);
    dc->vmsd = &vmstate_tpm_virtio;
    vdc->realize = tpm_virtio_realize;
    vdc->unrealize = tpm_virtio_unrealize;
    vdc->get_features = tpm_virtio_get_features;
    vdc->get_config = tpm_virtio_get_config;

    tc->model = TPM_MODEL_TPM_VIRTIO;
    tc->get_version = tpm_virtio_get_version;
    tc->request_completed = tpm_virtio_request_completed;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo tpm_virtio_info = {
    .name = TYPE_VIRTIO_TPM,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(TPMVirtioState),
    .class_init  = tpm_virtio_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_virtio_register(void)
{
    type_register_static(&tpm_virtio_info);
}

type_init(tpm_virtio_register)
