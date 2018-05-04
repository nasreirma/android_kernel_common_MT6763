#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/mm.h>

#include <linux/tee_ioc.h>
#include "tee_core.h"

#include "tee_ta_mgmt.h"
#include "tee_shm.h"
#include "tee_supp_com.h"

int tee_install_sys_ta(struct tee *tee, struct tee_ta_inst_desc *tee_ta_inst_desc)
{
	int r;
	void *shm_kva;
	unsigned long left;

	TEEC_UUID uuid;
	struct tee_rpc_invoke inv;
	struct tee_shm *shm;


	if (copy_from_user(&uuid, tee_ta_inst_desc->uuid, sizeof(TEEC_UUID))) {
		return -EFAULT;
	}

	if ((shm = tee_shm_alloc_from_rpc(tee, sizeof(TEEC_UUID) + sizeof(uint32_t) + tee_ta_inst_desc->ta_buf_size, TEEC_MEM_NONSECURE)) == NULL) {
		pr_err("%s: tee_shm_alloc_ns(%uB) failed\n", __func__, tee_ta_inst_desc->ta_buf_size);
		return -ENOMEM;
	}

	if ((shm_kva = vmap(shm->ns.pages, shm->ns.nr_pages, VM_MAP, PAGE_KERNEL)) == NULL) {
		pr_err("%s: failed to vmap %zu pages\n", __func__, shm->ns.nr_pages);
		r = -ENOMEM;
		goto exit;
	}

	memcpy(shm_kva, &uuid, sizeof(TEEC_UUID));
	memcpy((char *) shm_kva + sizeof(TEEC_UUID), &tee_ta_inst_desc->ta_buf_size, sizeof(uint32_t));

	if ((left = copy_from_user(
		(char *) shm_kva + sizeof(TEEC_UUID) + sizeof(uint32_t), tee_ta_inst_desc->ta_buf, tee_ta_inst_desc->ta_buf_size))) {
		pr_err("%s: copy_from_user failed size %x return: %lu \n", __func__, tee_ta_inst_desc->ta_buf_size, left);
		vunmap(shm_kva);
		r = -EFAULT;
		goto exit;
	}

	vunmap(shm_kva);

	memset(&inv, 0, sizeof(inv));

	inv.cmd = TEE_RPC_INSTALL_SYS_TA;
	inv.res = TEEC_ERROR_NOT_IMPLEMENTED;
	inv.nbr_bf = 1;

	inv.cmds[0].buffer = (void *) (unsigned long) shm->ns.token;
	inv.cmds[0].type = TEE_RPC_BUFFER | TEE_RPC_BUFFER_NONSECURE;
	inv.cmds[0].size = shm->size_req;

	if ((r = tee_supp_cmd(tee, TEE_RPC_ICMD_INVOKE, &inv, sizeof(inv)))) {
		pr_err("tee_supp_cmd failed with %d\n", r);
	} else {
		r = inv.res;
	}

exit:
	tee_shm_free_from_rpc(shm);

	return r;
}
