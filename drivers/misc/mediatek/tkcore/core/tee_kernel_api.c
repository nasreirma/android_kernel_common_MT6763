#include <linux/module.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

#include "linux/tee_kernel_api.h"
#include "linux/tee_core.h"
#include "linux/tee_ioc.h"

#include "tee_core_priv.h"
#include "tee_shm.h"
#include "tee_supp_com.h"

#define TEE_TZ_DEVICE_NAME	"tkcoredrv"

static void reset_tee_cmd(struct tee_cmd_io *cmd)
{
	memset(cmd, 0, sizeof(struct tee_cmd_io));
	cmd->fd_sess = -1;
	cmd->cmd = 0;
	cmd->uuid = NULL;
	cmd->origin = TEEC_ORIGIN_API;
	cmd->err = TEEC_SUCCESS;
	cmd->data = NULL;
	cmd->data_size = 0;
	cmd->op = NULL;
}

TEEC_Result TEEC_InitializeContext(const char *name, TEEC_Context *context)
{
	struct tee *tee;
	struct tee_context *ctx;
#ifdef TKCORE_KDBG
	pr_cont("%s: > name=\"%s\"\n", __func__, name);
#endif

	if (!context)
		return TEEC_ERROR_BAD_PARAMETERS;

	context->fd = 0;

	if (name == NULL)
		strncpy(context->devname, TEE_TZ_DEVICE_NAME,
			sizeof(context->devname));
	else
		strncpy(context->devname, name, sizeof(context->devname));

	tee = tee_get_tee(context->devname);
	if (!tee) {
		pr_err("%s - can't get device [%s]\n", __func__, name);
		return TEEC_ERROR_BAD_PARAMETERS;
	}

	ctx = tee_context_create(tee);
	if (IS_ERR_OR_NULL(ctx)) {
		pr_err("%s ctx is NULL\n", __func__);
		return TEEC_ERROR_BAD_PARAMETERS;
	}

	ctx->usr_client = 0;

	if ((context->fd = tee_core_alloc_uuid(ctx)) < 0) {

		if (context->fd == -ENOSPC || context->fd == -ENOMEM) {
			return TEEC_ERROR_OUT_OF_MEMORY;
		}

		if (context->fd == -EINVAL) {
			pr_err("%s context->fd is invalid\n", __func__);
			return TEEC_ERROR_BAD_PARAMETERS;
		}

		return TEEC_ERROR_GENERIC;
	}

#ifdef TKCORE_KDBG
	pr_cont("%s: < ctx=%p is created\n", __func__, (void *)ctx);
#endif
	return TEEC_SUCCESS;
}

EXPORT_SYMBOL(TEEC_InitializeContext);

void TEEC_FinalizeContext(TEEC_Context *context)
{
	struct tee_context *ctx;
	if (!context || !context->fd) {
		pr_err("%s - can't release context %p:[%s]\n", __func__,
			context, (context && context->devname) ? context->devname : "");
		return;
	}

	if ((ctx = tee_core_uuid2ptr(context->fd)) == NULL) {
		pr_err("%s bad context->fd %d provided\n", __func__, context->fd);
		return;
	}

	tee_core_free_uuid(context->fd);
	tee_context_destroy(ctx);
	return;
}
EXPORT_SYMBOL(TEEC_FinalizeContext);

TEEC_Result TEEC_OpenSession(TEEC_Context *context,
	TEEC_Session *session,
	const TEEC_UUID *destination,
	uint32_t connectionMethod,
	const void *connectionData,
	TEEC_Operation *operation,
	uint32_t *return_origin)
{
	TEEC_Operation dummy_op;
	struct tee_cmd_io cmd;
	struct tee_session *sess;
	struct tee_context *ctx;
	uint32_t error_origin;

	if (!operation) {
		/*
		 * The code here exist because Global Platform API states that
		 * it is allowed to give operation as a NULL pointer.
		 * In kernel and secure world we in most cases don't want
		 * this to be NULL, hence we use this dummy operation when
		 * a client doesn't provide any operation.
		 */
		memset(&dummy_op, 0, sizeof(TEEC_Operation));
		operation = &dummy_op;
	}

	if (!context || !session || !destination || !operation)
		return TEEC_ERROR_BAD_PARAMETERS;

	session->fd = 0;

	if ((ctx = tee_core_uuid2ptr(context->fd)) == NULL) {
		pr_err("%s bad context_fd %d provided\n",
			__func__, context->fd);
		if (return_origin) {
			*return_origin = TEEC_ORIGIN_COMMS;
		}
		return TEEC_ERROR_BAD_PARAMETERS;
	}

	reset_tee_cmd(&cmd);
	cmd.op = operation;
	cmd.uuid = (TEEC_UUID *) destination;

	sess = tee_session_create_and_open(ctx, &cmd);
	if (IS_ERR_OR_NULL(sess)) {
		if (cmd.origin)
			error_origin = cmd.origin;
		else
			error_origin = TEEC_ORIGIN_COMMS;

		if (return_origin)
			*return_origin = error_origin;

		if (cmd.err)
			return cmd.err;
		else
			return TEEC_ERROR_COMMUNICATION;
	} else {
		error_origin = cmd.origin;

		if ((session->fd = tee_core_alloc_uuid(sess)) < 0) {

			/* since we failed to alloc uuid, we need
			   to close this session */

			tee_session_close_and_destroy(sess);

			error_origin = TEEC_ORIGIN_COMMS;

			if (return_origin)
				*return_origin = error_origin;

			if (session->fd == -ENOSPC || session->fd == -ENOMEM) {
				return TEEC_ERROR_OUT_OF_MEMORY;
			}

			if (session->fd == -EINVAL)
				return TEEC_ERROR_BAD_PARAMETERS;

			return TEEC_ERROR_GENERIC;
		}

		if (return_origin)
			*return_origin = error_origin;

		return cmd.err;
	}
}
EXPORT_SYMBOL(TEEC_OpenSession);

void TEEC_CloseSession(TEEC_Session *session)
{
	if (session && session->fd) {
		struct tee_session *sess = tee_core_uuid2ptr(session->fd);

		if (sess == NULL) {
			pr_err("%s bad session_id %d provided\n",
				__func__, session->fd);
			return;
		}

		tee_core_free_uuid(session->fd);

		tee_session_close_and_destroy(sess);
	}
}
EXPORT_SYMBOL(TEEC_CloseSession);

TEEC_Result TEEC_InvokeCommand(TEEC_Session *session,
	uint32_t commandID,
	TEEC_Operation *operation,
	uint32_t *return_origin)
{
	int ret = 0;
	struct tee_cmd_io cmd;
	struct tee_session *sess;
	uint32_t error_origin;

	if (!session || !operation || !session->fd)
		return TEEC_ERROR_BAD_PARAMETERS;

	if ((sess = tee_core_uuid2ptr(session->fd)) == NULL) {
		pr_err("%s bad session_fd %d provided\n", __func__, session->fd);
		if (return_origin)
			*return_origin = TEEC_ORIGIN_COMMS;
		return TEEC_ERROR_BAD_PARAMETERS;
	}
	reset_tee_cmd(&cmd);
	cmd.cmd = commandID;
	cmd.op = operation;

	ret = tee_session_invoke_be(sess, &cmd);
	if (ret) {
		if (cmd.origin)
			error_origin = cmd.origin;
		else
			error_origin = TEEC_ORIGIN_COMMS;

		if (return_origin)
			*return_origin = error_origin;

		if (cmd.err)
			return cmd.err;
		else
			return TEEC_ERROR_COMMUNICATION;
	} else {
		if (return_origin)
			*return_origin = cmd.origin;
		return cmd.err;
	}
}
EXPORT_SYMBOL(TEEC_InvokeCommand);

TEEC_Result TEEC_RegisterSharedMemory(TEEC_Context *context,
	TEEC_SharedMemory *sharedMem)
{
	if (!sharedMem)
		return TEEC_ERROR_BAD_PARAMETERS;

	sharedMem->registered = 1;
	return TEEC_SUCCESS;
}
EXPORT_SYMBOL(TEEC_RegisterSharedMemory);

TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context *context,
	TEEC_SharedMemory *shared_memory)
{
	struct tee_shm_io shm_io;
	int ret;
	struct tee_shm *shm;

	if (!context || !context->ctx || !shared_memory)
		return TEEC_ERROR_BAD_PARAMETERS;

	shm_io.size = shared_memory->size;
	shm_io.flags = shared_memory->flags | TEEC_MEM_KAPI;
	ret = tee_shm_alloc_io(context->ctx, &shm_io);
	if (ret) {
		pr_err("%s: tee_shm_alloc_io(%zd) failed\n", __func__,
			shared_memory->size);
		return TEEC_ERROR_OUT_OF_MEMORY;
	}

	shared_memory->registered = 0;
	shared_memory->flags = shm_io.flags;
	shared_memory->d.fd = shm_io.fd_shm;

	shm = (struct tee_shm *)(long)shm_io.fd_shm;
	shared_memory->buffer = shm->resv.kaddr;
#ifdef TKCORE_KDBG
	pr_debug("%s(%d) => fd=%d, kaddr=%p\n", __func__,
		 shm_io.size, shm_io.fd_shm, (void *)shared_memory->buffer);
#endif

	return TEEC_SUCCESS;
}
EXPORT_SYMBOL(TEEC_AllocateSharedMemory);

void TEEC_ReleaseSharedMemory(TEEC_SharedMemory *shared_memory)
{
	struct tee_shm *shm;

	if (!shared_memory || shared_memory->registered)
		return;
#ifdef TKCORE_KDBG
	pr_debug("%s (vaddr = %p)\n", __func__, shared_memory->buffer);
#endif

	shm = (struct tee_shm *)(long)shared_memory->d.fd;
	tee_shm_free_io(shm);

	shared_memory->buffer = NULL;
	shared_memory->d.fd = 0;
}
EXPORT_SYMBOL(TEEC_ReleaseSharedMemory);
