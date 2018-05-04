#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>

#include <linux/tee_fp.h>

#include <tee_kernel_api.h>

#include "tee_fp_priv.h"

static TEEC_UUID SENSOR_DETECTOR_TA_UUID = { 0x966d3f7c, 0x04ef, 0x1beb, \
	{ 0x08, 0xb7, 0x57, 0xf3, 0x7a, 0x6d, 0x87, 0xf9 } };

#define CMD_READ_CHIPID		0x0
#define CMD_DISABLE			0x1
#define CMD_CONFIG_PADSEL	0x2

int tee_spi_cfg_padsel(uint32_t padsel)
{
	TEEC_Context context;
	TEEC_Session session;
	TEEC_Operation op;

	TEEC_Result r;

	uint32_t returnOrigin;

	printk("%s padsel=0x%x\n", __func__, padsel);

	memset(&context, 0, sizeof(context));
	memset(&session, 0, sizeof(session));
	memset(&op, 0, sizeof(op));

	if ((r = TEEC_InitializeContext(NULL, &context)) != TEEC_SUCCESS) {
		pr_err("TEEC_InitializeContext() failed with 0x%08x\n", r);
		return r;
	}
	if ((r = TEEC_OpenSession(
		&context, &session, &SENSOR_DETECTOR_TA_UUID,
		TEEC_LOGIN_PUBLIC,
		NULL, NULL, &returnOrigin)) != TEEC_SUCCESS) {
		pr_err("%s TEEC_OpenSession failed with 0x%x returnOrigun: %u\n",
			__func__, r, returnOrigin);
		TEEC_FinalizeContext(&context);
		return r;
	}

	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_VALUE_INPUT,
		TEEC_NONE,
		TEEC_NONE,
		TEEC_NONE);

	op.params[0].value.a = padsel;

	if ((r = TEEC_InvokeCommand(&session, CMD_CONFIG_PADSEL, &op, &returnOrigin)) != TEEC_SUCCESS) {
		pr_err("%s TEEC_InvokeCommand() failed with 0x%08x returnOrigin: %u\n",
			__func__, r, returnOrigin);
	}

	TEEC_CloseSession(&session);
	TEEC_FinalizeContext(&context);

	return r;
}

EXPORT_SYMBOL(tee_spi_cfg_padsel);

int tee_spi_transfer(void *conf, uint32_t conf_size, void *inbuf, void *outbuf, uint32_t size)
{
	TEEC_Context context;
	TEEC_Session session;
	TEEC_Operation op;

	TEEC_Result r;

	char *buf;


	uint32_t returnOrigin;

	printk("%s conf=%p conf_size=%u inbuf=%p outbuf=%p size=%u\n",
		__func__, conf, conf_size, inbuf, outbuf, size);

	if (!conf || !inbuf || !outbuf) {
		pr_err("Bad parameters NULL buf\n");
		return -EINVAL;
	}

	if (size == 0) {
		pr_err("zero buf size\n");
		return -EINVAL;
	}

	memset(&context, 0, sizeof(context));
	memset(&session, 0, sizeof(session));
	memset(&op, 0, sizeof(op));

	memcpy(outbuf, inbuf, size);

	if ((r = TEEC_InitializeContext(NULL, &context)) != TEEC_SUCCESS) {
		pr_err("TEEC_InitializeContext() failed with 0x%08x\n", r);
		return r;
	}
	if ((r = TEEC_OpenSession(
		&context, &session, &SENSOR_DETECTOR_TA_UUID,
		TEEC_LOGIN_PUBLIC,
		NULL, NULL, &returnOrigin)) != TEEC_SUCCESS) {
		pr_err("%s TEEC_OpenSession failed with 0x%x returnOrigun: %u\n",
			__func__, r, returnOrigin);
		TEEC_FinalizeContext(&context);
		return r;
	}

	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_MEMREF_TEMP_INPUT,
		TEEC_MEMREF_TEMP_INOUT,
		TEEC_NONE,
		TEEC_NONE);

	op.params[0].tmpref.buffer = conf;
	op.params[0].tmpref.size = conf_size;

	op.params[1].tmpref.buffer = outbuf;
	op.params[1].tmpref.size = size;

	buf = outbuf;

	if ((r = TEEC_InvokeCommand(&session, CMD_READ_CHIPID, &op, &returnOrigin)) != TEEC_SUCCESS) {
		pr_err("%s TEEC_InvokeCommand() failed with 0x%08x returnOrigin: %u\n",
			__func__, r, returnOrigin);
	}

	TEEC_CloseSession(&session);
	TEEC_FinalizeContext(&context);

	printk("[0x%02x 0x%02x 0x%02x 0x%02x]\n",
		buf[0], buf[1], buf[2], buf[3]);

	return r;
}

EXPORT_SYMBOL(tee_spi_transfer);

int tee_spi_transfer_disable(void)
{
	TEEC_Context context;
	TEEC_Session session;
	TEEC_Operation op;

	TEEC_Result r;

	uint32_t returnOrigin;

	memset(&context, 0, sizeof(context));
	memset(&session, 0, sizeof(session));
	memset(&op, 0, sizeof(op));

	if ((r = TEEC_InitializeContext(NULL, &context)) != TEEC_SUCCESS) {
		pr_err("TEEC_InitializeContext() failed with 0x%08x\n", r);
		return r;
	}

	if ((r = TEEC_OpenSession(
		&context, &session, &SENSOR_DETECTOR_TA_UUID,
		TEEC_LOGIN_PUBLIC,
		NULL, NULL, &returnOrigin)) != TEEC_SUCCESS) {
		pr_err("%s TEEC_OpenSession failed with 0x%x returnOrigun: %u\n",
			__func__, r, returnOrigin);
		TEEC_FinalizeContext(&context);
		return r;
	}

	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_NONE,
		TEEC_NONE,
		TEEC_NONE,
		TEEC_NONE);

	if ((r = TEEC_InvokeCommand(&session, CMD_DISABLE, &op, &returnOrigin)) != TEEC_SUCCESS) {
		pr_err("%s TEEC_InvokeCommand() failed with 0x%08x returnOrigin: %u\n",
			__func__, r, returnOrigin);
	}

	TEEC_CloseSession(&session);
	TEEC_FinalizeContext(&context);

	return r;
}

EXPORT_SYMBOL(tee_spi_transfer_disable);

int tee_fp_init(void)
{
	return 0;
}

void tee_fp_exit(void)
{
}
