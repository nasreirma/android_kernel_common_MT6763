#include <linux/slab.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>
#include <linux/fs.h>

#include "tee_shm.h"
#include "tee_core_priv.h"
#include "tee_tui_hal.h"

static int _init_tee_cmd(struct tee_session *sess, struct tee_cmd_io *cmd_io,
			 struct tee_cmd *cmd);
static void _update_client_tee_cmd(struct tee_session *sess,
				   struct tee_cmd_io *cmd_io,
				   struct tee_cmd *cmd);
static void _release_tee_cmd(struct tee_session *sess, struct tee_cmd *cmd);

#define _DEV_TEE _DEV(sess->ctx->tee)
#ifdef TKCORE_KDBG
#define INMSG dev_dbg(_DEV_TEE, "%s: >\n", __func__)
#define OUTMSG(val) dev_dbg(_DEV_TEE, "%s: < %d\n", __func__, (int)val)
#else
#define INMSG		do {} while (0)
#define OUTMSG(val) do {} while(0)
#endif

/******************************************************************************/

static inline bool flag_set(int val, int flags)
{
	return (val & flags) == flags;
}

static inline bool is_mapped_temp(int flags)
{
	return flag_set(flags, TEE_SHM_MAPPED | TEE_SHM_TEMP);
}


/******************************************************************************/
#ifdef TKCORE_KDBG
#define _UUID_STR_SIZE 35
static char *_uuid_to_str(const TEEC_UUID *uuid)
{
	static char uuid_str[_UUID_STR_SIZE];

	if (uuid) {
		sprintf(uuid_str,
			"%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",
			uuid->timeLow, uuid->timeMid, uuid->timeHiAndVersion,
			uuid->clockSeqAndNode[0], uuid->clockSeqAndNode[1],
			uuid->clockSeqAndNode[2], uuid->clockSeqAndNode[3],
			uuid->clockSeqAndNode[4], uuid->clockSeqAndNode[5],
			uuid->clockSeqAndNode[6], uuid->clockSeqAndNode[7]);
	} else {
		sprintf(uuid_str, "NULL");
	}

	return uuid_str;
}
#endif

static int tee_copy_from_user(struct tee_context *ctx, void *to, void *from,
		size_t size)
{
	if ((!to) || (!from) || (!size))
		return 0;
	if (ctx->usr_client)
		return copy_from_user(to, from, size);
	else {
		memcpy(to, from, size);
		return 0;
	}
}

static int tee_copy_to_user(struct tee_context *ctx, void *to, void *from,
		size_t size)
{
	if ((!to) || (!from) || (!size))
		return 0;
	if (ctx->usr_client)
		return copy_to_user(to, from, size);
	else {
		memcpy(to, from, size);
		return 0;
	}
}

/* Defined as macro to let the put_user macro see the types */
#define tee_put_user(ctx, from, to)				\
	do {							\
		if ((ctx)->usr_client)				\
			put_user(from, to);			\
		else						\
			*to = from;				\
	} while (0)

static inline int tee_session_is_opened(struct tee_session *sess)
{
	if (sess && sess->sessid)
		return (sess->sessid != 0);
	return 0;
}

static int tee_session_open_be(struct tee_session *sess,
		struct tee_cmd_io *cmd_io)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd cmd;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > open a new session", __func__);
#endif

	sess->sessid = 0;
	ret = _init_tee_cmd(sess, cmd_io, &cmd);
	if (ret)
		goto out;

	if (cmd.uuid) {
#ifdef TKCORE_KDBG
		dev_dbg(_DEV(tee), "%s: UUID=%s\n", __func__,
			_uuid_to_str((TEEC_UUID *) cmd.uuid->resv.kaddr));
#endif
	}

	ret = tee->ops->open(sess, &cmd);
	if (ret == 0)
		_update_client_tee_cmd(sess, cmd_io, &cmd);
	else {
		/* propagate the reason of the error */
		cmd_io->origin = cmd.origin;
		cmd_io->err = cmd.err;
	}

out:
	_release_tee_cmd(sess, &cmd);
#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d, sessid=%08x", __func__, ret,
		sess->sessid);
#endif
	return ret;
}

int tee_session_invoke_be(struct tee_session *sess, struct tee_cmd_io *cmd_io)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd cmd;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > sessid=%08x, cmd=0x%08x\n", __func__,
		sess->sessid, cmd_io->cmd);
#endif

	ret = _init_tee_cmd(sess, cmd_io, &cmd);
	if (ret)
		goto out;

	ret = tee->ops->invoke(sess, &cmd);
	if (!ret)
		_update_client_tee_cmd(sess, cmd_io, &cmd);
	else {
		/* propagate the reason of the error */
		cmd_io->origin = cmd.origin;
		cmd_io->err = cmd.err;
	}

out:
	_release_tee_cmd(sess, &cmd);
#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
#endif
	return ret;
}

static int tee_session_close_be(struct tee_session *sess)
{
	int ret = -EINVAL;
	struct tee *tee;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > sessid=%08x", __func__, sess->sessid);
#endif

	ret = tee->ops->close(sess);
	sess->sessid = 0;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
#endif
	return ret;
}

static int tee_session_cancel_be(struct tee_session *sess,
				 struct tee_cmd_io *cmd_io)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd cmd;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > sessid=%08x, cmd=0x%08x\n", __func__,
		sess->sessid, cmd_io->cmd);
#endif

	ret = _init_tee_cmd(sess, cmd_io, &cmd);
	if (ret)
		goto out;

	ret = tee->ops->cancel(sess, &cmd);

out:
	_release_tee_cmd(sess, &cmd);
#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
#endif
	return ret;
}

static int tee_do_invoke_command(struct tee_session *sess,
				 struct tee_cmd_io __user *u_cmd)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd_io k_cmd;
	struct tee_context *ctx;

	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);
	ctx = sess->ctx;
	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > sessid=%08x\n", __func__, sess->sessid);
#endif

	BUG_ON(!sess->sessid);

	if (tee_copy_from_user
		(ctx, &k_cmd, (void *)u_cmd, sizeof(struct tee_cmd_io))) {
		dev_err(_DEV(tee), "%s: tee_copy_from_user failed\n", __func__);
		goto exit;
	}

	if ((k_cmd.op == NULL) || (k_cmd.uuid != NULL) ||
		(k_cmd.data != NULL) || (k_cmd.data_size != 0)) {
		dev_err(_DEV(tee),
			"%s: op or/and data parameters are not valid\n",
			__func__);
		goto exit;
	}

	ret = tee_session_invoke_be(sess, &k_cmd);
	if (ret) {
		dev_err(_DEV(tee), "%s: tee_invoke_command failed\n", __func__);
	}

	tee_put_user(ctx, k_cmd.err, &u_cmd->err);
	tee_put_user(ctx, k_cmd.origin, &u_cmd->origin);

exit:
#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d\n", __func__, ret);
#endif
	return ret;
}

static int tee_do_cancel_cmd(struct tee_session *sess,
		struct tee_cmd_io __user *u_cmd)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_cmd_io k_cmd;
	struct tee_context *ctx;

	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);
	ctx = sess->ctx;
	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(sess->ctx->tee->dev, "%s: > sessid=%08x\n", __func__,
		sess->sessid);
#endif

	BUG_ON(!sess->sessid);

	if (tee_copy_from_user
		(ctx, &k_cmd, (void *)u_cmd, sizeof(struct tee_cmd_io))) {
		dev_err(_DEV(tee), "%s: tee_copy_from_user failed\n", __func__);
		goto exit;
	}

	if ((k_cmd.op == NULL) || (k_cmd.uuid != NULL) ||
		(k_cmd.data != NULL) || (k_cmd.data_size != 0)) {
		dev_err(_DEV(tee),
			"%s: op or/and data parameters are not valid\n",
			__func__);
		goto exit;
	}

	ret = tee_session_cancel_be(sess, &k_cmd);
	if (ret) {
		dev_err(_DEV(tee), "%s: tee_invoke_command failed\n", __func__);
	}

	tee_put_user(ctx, k_cmd.err, &u_cmd->err);
	tee_put_user(ctx, k_cmd.origin, &u_cmd->origin);

exit:
#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
#endif
	return ret;
}

static int tee_do_kernel_cancel_cmd(struct tee_session *sess,
		struct tee_cmd_io *k_cmd)
{
	int ret = -EINVAL;
	struct tee *tee;
	struct tee_context *ctx;
	struct tee_cmd cmd;

	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);
	ctx = sess->ctx;
	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(sess->ctx->tee->dev, "%s: > sessid=%08x\n", __func__,
		sess->sessid);
#endif

	BUG_ON(!sess->sessid);

	if ((k_cmd == NULL || k_cmd->op == NULL) || (k_cmd->uuid != NULL) ||
		(k_cmd->data != NULL) || (k_cmd->data_size != 0)) {
		dev_err(_DEV(tee),
			"%s: op or/and data parameters are not valid\n",
			__func__);
		goto exit;
	}

	cmd.cmd = k_cmd->cmd;
	cmd.origin = TEEC_ORIGIN_TEE;
	cmd.err = TEEC_ERROR_BAD_PARAMETERS;

	cmd.param.type_original = 0;

	ret = tee->ops->cancel(sess, &cmd);

	if (ret) {
		dev_err(_DEV(tee), "%s: tee_cancel failed\n", __func__);
	}

exit:
#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d", __func__, ret);
#endif
	return ret;
}

DECLARE_COMPLETION(io_comp);
static int flag_tui_obj = 0;
struct tui_obj {
	struct tee_cmd_io op; // cached operation
	struct tee_session *sess;
	uint32_t cmd_id;
	uint32_t status;
	struct mutex lock;
} g_tui_obj;

static uint32_t send_cmd_to_user(uint32_t command_id)
{
	uint32_t ret = 0;

	g_tui_obj.cmd_id = command_id;
	complete(&io_comp);
#ifdef TKCORE_KDBG
	pr_debug("send_cmd_to_user: cmd %d done!\n", command_id);
#endif

	return ret;
}

bool teec_notify_event(uint32_t event_type)
{
	bool ret = false;

	// Currently we only support TUI cancel event type
	printk("TUI teec_notify_event: event_type is %d\n", event_type);

	// Cancel the TUI session if exists
	if(g_tui_obj.status) {
		ret = tee_do_kernel_cancel_cmd(g_tui_obj.sess, &g_tui_obj.op);
	}

	return ret;
}
EXPORT_SYMBOL(teec_notify_event);

int teec_wait_cmd(uint32_t *cmd_id)
{
	/* Wait for signal from DCI handler */
	wait_for_completion(&io_comp);
#ifdef INIT_COMPLETION
	INIT_COMPLETION(io_comp);
#else
	io_comp.done = 0;
#endif

	*cmd_id = g_tui_obj.cmd_id;
	return 0;
}
EXPORT_SYMBOL(teec_wait_cmd);

static long tee_session_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct tee *tee;
	struct tee_session *sess = filp->private_data;
	int ret;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);

	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > cmd nr=%d\n", __func__, _IOC_NR(cmd));
#endif

	switch (cmd) {
	case TEE_INVOKE_COMMAND_IOC:
		ret =
			tee_do_invoke_command(sess,
					  (struct tee_cmd_io __user *)arg);
		break;
	case TEE_REQUEST_CANCELLATION_IOC:
		ret = tee_do_cancel_cmd(sess, (struct tee_cmd_io __user *)arg);
		break;
	case TEE_TUI_OPEN_SESSION_IOC:
#ifdef TKCORE_KDBG
		dev_dbg(_DEV(tee), "%s: TEE_TUI_OPEN_SESSION_IOC.\n", __func__);
#endif
		if(flag_tui_obj == 0) {
			mutex_init(&g_tui_obj.lock);
			flag_tui_obj = 1;
		}
		mutex_lock(&g_tui_obj.lock);
		if(g_tui_obj.status != 0 || (struct tee_cmd_io __user *)arg == NULL) {
			ret = -EBUSY;
			mutex_unlock(&g_tui_obj.lock);
#ifdef TKCORE_KDBG
		dev_dbg(_DEV(tee), "%s: TEE_TUI_OPEN_SESSION_IOC: tui busy or invalid argument\n", __func__);
#endif
			break;
		}
		if (tee_copy_from_user(sess->ctx, &g_tui_obj.op, (void *)arg, sizeof(struct tee_cmd_io))) {
			ret = -EINVAL;
			mutex_unlock(&g_tui_obj.lock);
			break;
		}
		//reset part of op
		g_tui_obj.op.uuid = NULL;
		g_tui_obj.op.data = NULL;
		g_tui_obj.op.data_size = 0;

		g_tui_obj.sess = sess;
		g_tui_obj.status = 1;
		mutex_unlock(&g_tui_obj.lock);
		/* Start android TUI activity */
		ret = send_cmd_to_user(TEEC_TUI_CMD_START_ACTIVITY);
		if (0 != ret) {
			mutex_lock(&g_tui_obj.lock);
			g_tui_obj.status = 0;
			mutex_unlock(&g_tui_obj.lock);
			break;
		}
		/* Deactivate linux UI drivers */
		ret = hal_tui_deactivate();
		if (0 != ret) {
			mutex_lock(&g_tui_obj.lock);
			g_tui_obj.status = 0;
			mutex_unlock(&g_tui_obj.lock);
			send_cmd_to_user(TEEC_TUI_CMD_STOP_ACTIVITY);
			break;
		}
		break;
	case TEE_TUI_CLOSE_SESSION_IOC:
#ifdef TKCORE_KDBG
		dev_dbg(_DEV(tee), "%s: TEE_TUI_CLOSE_SESSION_IOC.\n", __func__);
#endif
		if(flag_tui_obj == 0) {
			mutex_init(&g_tui_obj.lock);
			flag_tui_obj = 1;
		}
		mutex_lock(&g_tui_obj.lock);
		g_tui_obj.status = 0;
		mutex_unlock(&g_tui_obj.lock);
		/* Activate linux UI drivers */
		ret = hal_tui_activate();
		/* Stop android TUI activity */
		ret = send_cmd_to_user(TEEC_TUI_CMD_STOP_ACTIVITY);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d\n", __func__, ret);
#endif

	return ret;
}

static int tee_session_release(struct inode *inode, struct file *filp)
{
	struct tee_session *sess = filp->private_data;
	int ret = 0;
	struct tee *tee;

	BUG_ON(!sess || !sess->ctx || !sess->ctx->tee);
	tee = sess->ctx->tee;

	ret = tee_session_close_and_destroy(sess);
	return ret;
}

const struct file_operations tee_session_fops = {
	.owner = THIS_MODULE,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tee_session_ioctl,
#endif
	.unlocked_ioctl = tee_session_ioctl,
	.release = tee_session_release,
};

int tee_session_close_and_destroy(struct tee_session *sess)
{
	int ret;
	struct tee *tee;
	struct tee_context *ctx;

	if (!sess || !sess->ctx || !sess->ctx->tee)
		return -EINVAL;

	ctx = sess->ctx;
	tee = ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > sess=%p\n", __func__, sess);
#endif

	if (!tee_session_is_opened(sess))
		return -EINVAL;

	ret = tee_session_close_be(sess);

	mutex_lock(&tee->lock);
	tee_dec_stats(&tee->stats[TEE_STATS_SESSION_IDX]);
	list_del(&sess->entry);

	devm_kfree(_DEV(tee), sess);
	tee_context_put(ctx);
	tee_put(tee);
	mutex_unlock(&tee->lock);

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: <\n", __func__);
#endif
	return ret;
}

struct tee_session *tee_session_create_and_open(struct tee_context *ctx,
						struct tee_cmd_io *cmd_io)
{
	int ret = 0;
	struct tee_session *sess;
	struct tee *tee;

	BUG_ON(!ctx->tee);

	tee = ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: >\n", __func__);
#endif
	ret = tee_get(tee);
	if (ret)
		return ERR_PTR(-EBUSY);

	sess = devm_kzalloc(_DEV(tee), sizeof(struct tee_session), GFP_KERNEL);
	if (!sess) {
		dev_err(_DEV(tee), "%s: tee_session allocation() failed\n",
			__func__);
		tee_put(tee);
		return ERR_PTR(-ENOMEM);
	}

	tee_context_get(ctx);
	sess->ctx = ctx;

	ret = tee_session_open_be(sess, cmd_io);
	mutex_lock(&tee->lock);
	if (ret || !sess->sessid || cmd_io->err) {
		dev_err(_DEV(tee), "%s: ERROR ret=%d (err=0x%08x, org=%d,  sessid=0x%08x)\n",
				__func__, ret, cmd_io->err,
				cmd_io->origin, sess->sessid);
		tee_put(tee);
		tee_context_put(ctx);
		devm_kfree(_DEV(tee), sess);
		mutex_unlock(&tee->lock);
		if (ret)
			return ERR_PTR(ret);
		else
			return NULL;
	}

	tee_inc_stats(&tee->stats[TEE_STATS_SESSION_IDX]);
	list_add_tail(&sess->entry, &ctx->list_sess);
	mutex_unlock(&tee->lock);

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < sess=%p\n", __func__, sess);
#endif
	return sess;
}

int tee_session_create_fd(struct tee_context *ctx, struct tee_cmd_io *cmd_io)
{
	int ret;
	struct tee_session *sess;
	struct tee *tee = ctx->tee;

	(void) tee;

	BUG_ON(cmd_io->fd_sess > 0);

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: >\n", __func__);
#endif

	sess = tee_session_create_and_open(ctx, cmd_io);
	if (IS_ERR_OR_NULL(sess)) {
		ret = PTR_ERR(sess);
#ifdef TKCORE_KDBG
		dev_dbg(_DEV(tee), "%s: ERROR can't create the session (ret=%d, err=0x%08x, org=%d)\n",
			__func__, ret, cmd_io->err, cmd_io->origin);
#endif
		cmd_io->fd_sess = -1;
		goto out;
	}

	/* Retrieve a fd */
	cmd_io->fd_sess = -1;
	ret =
		anon_inode_getfd("tee_session", &tee_session_fops, sess, O_CLOEXEC);
	if (ret < 0) {
		dev_err(_DEV(tee), "%s: ERROR can't get a fd (ret=%d)\n",
			__func__, ret);
		tee_session_close_and_destroy(sess);
		goto out;
	}
	cmd_io->fd_sess = ret;
	ret = 0;

out:
#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: < ret=%d, sess=%p, fd=%d\n", __func__,
		ret, sess, cmd_io->fd_sess);
#endif
	return ret;
}

static bool tee_session_is_supported_type(struct tee_session *sess, int type)
{
	switch (type) {
	case TEEC_NONE:
	case TEEC_VALUE_INPUT:
	case TEEC_VALUE_OUTPUT:
	case TEEC_VALUE_INOUT:
	case TEEC_MEMREF_TEMP_INPUT:
	case TEEC_MEMREF_TEMP_OUTPUT:
	case TEEC_MEMREF_TEMP_INOUT:
	case TEEC_MEMREF_WHOLE:
	case TEEC_MEMREF_PARTIAL_INPUT:
	case TEEC_MEMREF_PARTIAL_OUTPUT:
	case TEEC_MEMREF_PARTIAL_INOUT:
		return true;
	default:
		dev_err(_DEV_TEE, "type is invalid (type %02x)\n", type);
		return false;
	}
}

static int to_memref_type(int flags)
{
	if (flag_set(flags, TEEC_MEM_INPUT | TEEC_MEM_OUTPUT))
		return TEEC_MEMREF_TEMP_INOUT;

	if (flag_set(flags, TEEC_MEM_INPUT))
		return TEEC_MEMREF_TEMP_INPUT;

	if (flag_set(flags, TEEC_MEM_OUTPUT))
		return TEEC_MEMREF_TEMP_OUTPUT;

	pr_err("%s: bad flags=%x\n", __func__, flags);
	return 0;
}

static int _init_tee_cmd(struct tee_session *sess, struct tee_cmd_io *cmd_io,
			 struct tee_cmd *cmd)
{
	int ret = -EINVAL;
	int idx;
	TEEC_Operation op;
	struct tee_data *param = &cmd->param;
	struct tee *tee;
	struct tee_context *ctx;

	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);
	ctx = sess->ctx;
	tee = sess->ctx->tee;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV(tee), "%s: > sessid=%08x\n", __func__, sess->sessid);
#endif

	memset(cmd, 0, sizeof(struct tee_cmd));

	cmd->cmd = cmd_io->cmd;
	cmd->origin = TEEC_ORIGIN_TEE;
	cmd->err = TEEC_ERROR_BAD_PARAMETERS;
	cmd_io->origin = cmd->origin;
	cmd_io->err = cmd->err;

	if (tee_context_copy_from_client(ctx, &op, cmd_io->op, sizeof(op)))
		goto out;

	cmd->param.type_original = op.paramTypes;

	for (idx = 0; idx < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++idx) {
		uint32_t offset = 0;
		uint32_t size = 0;
		int type = TEEC_PARAM_TYPE_GET(op.paramTypes, idx);

		switch (type) {
		case TEEC_NONE:
			break;

		case TEEC_VALUE_INPUT:
		case TEEC_VALUE_OUTPUT:
		case TEEC_VALUE_INOUT:
			param->params[idx].value = op.params[idx].value;
#ifdef TKCORE_KDBG
			dev_dbg(_DEV_TEE,
				"%s: param[%d]:type=%d,a=%08x,b=%08x (VALUE)\n",
				__func__, idx, type, param->params[idx].value.a,
				param->params[idx].value.b);
#endif
			break;

		case TEEC_MEMREF_TEMP_INPUT:
		case TEEC_MEMREF_TEMP_OUTPUT:
		case TEEC_MEMREF_TEMP_INOUT:
#ifdef TKCORE_KDBG
			pr_info("%s > param[%d]:type=%d,buffer=%p,s=%zu (TMPREF)\n",
				__func__, idx, type, op.params[idx].tmpref.buffer,
				op.params[idx].tmpref.size);
#endif

			param->params[idx].shm = tee_context_create_tmpref_buffer(ctx,
				op.params[idx].tmpref.size,
				op.params[idx].tmpref.buffer,
				type);
			if (IS_ERR_OR_NULL(param->params[idx].shm))
				goto out;

#ifdef TKCORE_KDBG
			pr_info("%s < %d %p:%zd\n", __func__, idx,
					(void *) (unsigned long) param->params[idx].shm->resv.paddr,
					param->params[idx].shm->size_alloc);
#endif
			break;

		case TEEC_MEMREF_PARTIAL_INPUT:
		case TEEC_MEMREF_PARTIAL_OUTPUT:
		case TEEC_MEMREF_PARTIAL_INOUT:
		case TEEC_MEMREF_WHOLE:
			if (tee_copy_from_user(ctx, &param->c_shm[idx],
						op.params[idx].memref.parent,
						sizeof(param->c_shm[idx]))) {
				goto out;
			}

			if (type == TEEC_MEMREF_WHOLE) {
				offset = 0;
				size = param->c_shm[idx].size;
			} else { /* for PARTIAL, check the size */
				offset = op.params[idx].memref.offset;
				size = op.params[idx].memref.size;
				if (param->c_shm[idx].size < size + offset) {
					dev_err(_DEV(tee), "A PARTIAL parameter is bigger than the parent %zd < %d + %d\n",
						param->c_shm[idx].size, size,
						offset);
					goto out;
				}
			}

#ifdef TKCORE_KDBG
			dev_dbg(_DEV_TEE, "> param[%d]:type=%d,buffer=%p, offset=%d size=%d\n",
					idx, type, param->c_shm[idx].buffer,
					offset, size);
#endif

			type = to_memref_type(param->c_shm[idx].flags);
			if (type == 0)
				goto out;

			param->params[idx].shm = tee_shm_get(ctx,
					&param->c_shm[idx], size, offset);

			if (IS_ERR_OR_NULL(param->params[idx].shm)) {
				param->params[idx].shm =
					tee_context_create_tmpref_buffer(ctx, size,
						param->c_shm[idx].buffer + offset, type);

				if (IS_ERR_OR_NULL(param->params[idx].shm))
					goto out;
			}

#ifdef TKCORE_KDBG
			pr_info("%s < %d %p:%zd\n", __func__, idx,
				(void *) (unsigned long) param->params[idx].shm->resv.paddr,
				param->params[idx].shm->size_req);
#endif
			break;

		default:
			BUG_ON(1);
		}

		param->type |= (type << (idx * 4));
	}

	if (cmd_io->uuid != NULL) {
#ifdef TKCORE_KDBG
		dev_dbg(_DEV_TEE, "%s: copy UUID value...\n", __func__);
#endif
		cmd->uuid = tee_context_alloc_shm_tmp(sess->ctx,
			sizeof(*cmd_io->uuid), cmd_io->uuid, TEEC_MEM_INPUT);
		if (IS_ERR_OR_NULL(cmd->uuid)) {
			ret = -EINVAL;
			goto out;
		}
	}

	ret = 0;

out:
	if (ret)
		_release_tee_cmd(sess, cmd);

#ifdef TKCORE_KDBG
	dev_dbg(_DEV_TEE, "%s: < ret=%d\n", __func__, ret);
#endif
	return ret;
}

static void _update_client_tee_cmd(struct tee_session *sess,
				   struct tee_cmd_io *cmd_io,
				   struct tee_cmd *cmd)
{
	int idx;
	struct tee_context *ctx;
	TEEC_Operation tmp_op;

	BUG_ON(!cmd_io);
	BUG_ON(!cmd_io->op);
	BUG_ON(!cmd_io->op->params);
	BUG_ON(!cmd);
	BUG_ON(!sess->ctx);
	ctx = sess->ctx;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV_TEE, "%s: returned err=0x%08x (origin=%d)\n", __func__,
		cmd->err, cmd->origin);
#endif

	cmd_io->origin = cmd->origin;
	cmd_io->err = cmd->err;

	if (tee_context_copy_from_client(ctx, &tmp_op, cmd_io->op, sizeof(tmp_op))) {
		pr_err("Failed to copy op from client\n");
		return;
	}

	if (cmd->param.type_original == TEEC_PARAM_TYPES(TEEC_NONE,
			TEEC_NONE, TEEC_NONE, TEEC_NONE))
		return;

	for (idx = 0; idx < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++idx) {
		int type = TEEC_PARAM_TYPE_GET(cmd->param.type_original, idx);
		int offset = 0;
		size_t size;
		size_t size_new;
		TEEC_SharedMemory *parent;

#ifdef TKCORE_KDBG
		dev_dbg(_DEV_TEE, "%s: id %d type %d\n", __func__, idx, type);
#endif
		BUG_ON(!tee_session_is_supported_type(sess, type));
		switch (type) {
		case TEEC_NONE:
		case TEEC_VALUE_INPUT:
		case TEEC_MEMREF_TEMP_INPUT:
		case TEEC_MEMREF_PARTIAL_INPUT:
			break;
		case TEEC_VALUE_OUTPUT:
		case TEEC_VALUE_INOUT:
#ifdef TKCORE_KDBG
			dev_dbg(_DEV_TEE, "%s: a=%08x, b=%08x\n",
				__func__,
				cmd->param.params[idx].value.a,
				cmd->param.params[idx].value.b);
#endif
			if (tee_copy_to_user
				(ctx, &cmd_io->op->params[idx].value,
				&cmd->param.params[idx].value,
				sizeof(tmp_op.params[idx].value))) {

				dev_err(_DEV_TEE,
					"%s:%d: can't update %d result to user\n",
					__func__, __LINE__, idx);
			}
			break;
		case TEEC_MEMREF_TEMP_OUTPUT:
		case TEEC_MEMREF_TEMP_INOUT:
			/* Returned updated size */
			size_new = cmd->param.params[idx].shm->size_req;
			if (size_new !=
				tmp_op.params[idx].tmpref.size) {
#ifdef TKCORE_KDBG
				dev_dbg(_DEV_TEE,
					"Size has been updated by the TA %zd != %zd\n",
					size_new,
					tmp_op.params[idx].tmpref.size);
#endif
				tee_put_user(ctx, size_new,
					&cmd_io->op->params[idx].tmpref.size);
			}
#ifdef TKCORE_KDBG
			dev_dbg(_DEV_TEE, "%s: tmpref %p\n", __func__,
				cmd->param.params[idx].shm->resv.kaddr);
#endif

			/* ensure we do not exceed the shared buffer length */
			if (size_new > tmp_op.params[idx].tmpref.size) {
				dev_err(_DEV_TEE,
					"  *** Wrong returned size from %d:%zd > %zd\n",
					idx, size_new,
					tmp_op.params[idx].tmpref.size);
			} else if (tee_copy_to_user
				 (ctx,
				  tmp_op.params[idx].tmpref.buffer,
				  cmd->param.params[idx].shm->resv.kaddr,
				  size_new)) {
				dev_err(_DEV_TEE,
					"%s:%d: can't update %d result to user\n",
					__func__, __LINE__, idx);
			}
			break;

		case TEEC_MEMREF_PARTIAL_OUTPUT:
		case TEEC_MEMREF_PARTIAL_INOUT:
		case TEEC_MEMREF_WHOLE:
			if (type == TEEC_MEMREF_WHOLE) {
				offset = 0;
				size = parent->size;
			} else {
				offset = tmp_op.params[idx].memref.offset;
				size = tmp_op.params[idx].memref.size;
			}
			parent = &cmd->param.c_shm[idx];

			/* Returned updated size */
			size_new = cmd->param.params[idx].shm->size_req;
			tee_put_user(ctx, size_new,
					&cmd_io->op->params[idx].memref.size);

			/*
			 * If we allocated a tmpref buffer,
			 * copy back data to the user buffer
			 */
			if (is_mapped_temp(cmd->param.params[idx].shm->flags)) {
				if (parent->buffer &&
					offset + size_new <= parent->size) {
					if (tee_copy_to_user(ctx,
					   parent->buffer + offset,
					   cmd->param.params[idx].shm->resv.kaddr,
					   size_new)) {
							dev_err(_DEV_TEE,
								"%s: can't update %d data to user\n",
								__func__, idx);
					}
				}
			}
			break;
		default:
			BUG_ON(1);
		}
	}

}

static void _release_tee_cmd(struct tee_session *sess, struct tee_cmd *cmd)
{
	int idx;
	struct tee_context *ctx;

	BUG_ON(!cmd);
	BUG_ON(!sess);
	BUG_ON(!sess->ctx);
	BUG_ON(!sess->ctx->tee);

	ctx = sess->ctx;

#ifdef TKCORE_KDBG
	dev_dbg(_DEV_TEE, "%s: > free the temporary objects...\n", __func__);
#endif

	tee_shm_free(cmd->uuid);

	if (cmd->param.type_original == TEEC_PARAM_TYPES(TEEC_NONE,
			TEEC_NONE, TEEC_NONE, TEEC_NONE))
		goto out;

	for (idx = 0; idx < TEEC_CONFIG_PAYLOAD_REF_COUNT; ++idx) {
		int type = TEEC_PARAM_TYPE_GET(cmd->param.type_original, idx);
		struct tee_shm *shm;
		switch (type) {
		case TEEC_NONE:
		case TEEC_VALUE_INPUT:
		case TEEC_VALUE_OUTPUT:
		case TEEC_VALUE_INOUT:
			break;
		case TEEC_MEMREF_TEMP_INPUT:
		case TEEC_MEMREF_TEMP_OUTPUT:
		case TEEC_MEMREF_TEMP_INOUT:
		case TEEC_MEMREF_WHOLE:
		case TEEC_MEMREF_PARTIAL_INPUT:
		case TEEC_MEMREF_PARTIAL_OUTPUT:
		case TEEC_MEMREF_PARTIAL_INOUT:
			if (IS_ERR_OR_NULL(cmd->param.params[idx].shm))
				break;

			shm = cmd->param.params[idx].shm;

			if (is_mapped_temp(shm->flags))
				tee_shm_free(shm);
			else
				tee_shm_put(ctx, shm);
			break;
		default:
			BUG_ON(1);
		}
	}

out:
	memset(cmd, 0, sizeof(struct tee_cmd));
#ifdef TKCORE_KDBG
	dev_dbg(_DEV_TEE, "%s: <\n", __func__);
#endif
}
