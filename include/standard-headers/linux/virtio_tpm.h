#ifndef _LINUX_VIRTIO_TPM_H
#define _LINUX_VIRTIO_TPM_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */
#include "standard-headers/linux/virtio_types.h"

/* Feature bits */
#define VIRTIO_TPM_F_GET_BUFFERSIZE 1       /* TPM's buffer size */

struct virtio_tpm_config {
	/* TPM's buffer size (r/o); typically ~4k */
	__virtio16 buffersize;
        /* TPM version; 1 = TPM 1.2, 2 = TPM 2 */
	uint8_t    tpm_version;
} QEMU_PACKED;

/* Header to use when sending a TPM command to the device */
struct virtio_tpm_cmd_header {
	/* locality of the command */
	uint8_t locty;
} QEMU_PACKED;

/* Result from the device after processing TPM command */
struct virtio_tpm_cmd_result {
        /* one of the below results */
	uint8_t status;
} QEMU_PACKED;

/* Status codes */
#define VIRTIO_TPM_CMD_RESULT_SUCCESS      0
/* bad input such as buffers that are too short */
#define VIRTIO_TPM_CMD_RESULT_BAD_INPUT    1

#endif
