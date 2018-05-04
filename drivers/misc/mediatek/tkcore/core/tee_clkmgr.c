#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/tee_clkmgr.h>
#include <linux/tee_client_api.h>

#include "tee_clkmgr_priv.h"

#define TAG "TKCORE-clkmgr: "

typedef struct {
	uint32_t token;
	tee_clk_fn e, d;
	const void *p0, *p1, *p2;
	size_t argnum;
	struct list_head le;
} clkmgr_handle_t;

/* sync with tee-os */
enum tee_clkmgr_type {
	TEE_CLKMGR_TYPE_SPI = 0,
	TEE_CLKMGR_TYPE_I2C,
	TEE_CLKMGR_TYPE_I2C_DMA
};

typedef void (* tee_clk_fn_0) (void);
typedef void (* tee_clk_fn_1) (const void *);
typedef void (* tee_clk_fn_2) (const void *, const void *);
typedef void (* tee_clk_fn_3) (const void *, const void *, const void *);

static const char *clkid[] = {
	[ TEE_CLKMGR_TYPE_SPI ] = "spi",
	[ TEE_CLKMGR_TYPE_I2C ] = "i2c",
	[ TEE_CLKMGR_TYPE_I2C_DMA ] = "i2c-dma",
};

static LIST_HEAD(clk_list);
static spinlock_t clk_list_lock;

/* called inside list_lock */
static clkmgr_handle_t *get_clkmgr_handle(uint32_t token)
{
	clkmgr_handle_t *h;

	list_for_each_entry(h, &clk_list, le) {
		if (h->token == token)
			return h;
	}

	return NULL;
}

int tee_clkmgr_handle(uint32_t token, uint32_t op)
{
	clkmgr_handle_t *ph, h;
	tee_clk_fn fn;

	spin_lock(&clk_list_lock);

	ph = get_clkmgr_handle(token | TEE_CLKMGR_TOKEN_NOT_LEGACY);
	if (ph == NULL) {
		pr_err(TAG "invalid token %u\n", token);
		spin_unlock(&clk_list_lock);
		return TEEC_ERROR_ITEM_NOT_FOUND;
	}

	memcpy(&h, ph, sizeof(h));

	spin_unlock(&clk_list_lock);

	fn = (op & TEE_CLKMGR_OP_ENABLE) ? h.e : h.d;

	if (h.argnum == 0) {
		((tee_clk_fn_0) fn) ();
	} else if (h.argnum == 1) {
		((tee_clk_fn_1) fn) (h.p0);
	} else if (h.argnum == 2) {
		((tee_clk_fn_2) fn) (h.p0, h.p1);
	} else if (h.argnum == 3) {
		((tee_clk_fn_3) fn) (h.p0, h.p1, h.p2);
	} else {
		pr_err(TAG "unsupported token %u argnum %zu\n", h.token, h.argnum);
		return TEEC_ERROR_NOT_SUPPORTED;
	}

	return 0;
}

EXPORT_SYMBOL(tee_clkmgr_handle);

int tee_clkmgr_register(const char *clkname, int id, tee_clk_fn e, tee_clk_fn d,
	void *p0, void *p1, void *p2, size_t argnum)
{
	size_t n;

	clkmgr_handle_t *h, *w;

	if (argnum > 3) {
		pr_err(TAG "does not support argnum %zu\n", argnum);
		return -EINVAL;
	}

	for (n = 0; n < sizeof(clkid) / sizeof(clkid[0]); n++) {
		if (clkid[n] && strcmp(clkname, clkid[n]) == 0) {
			break;
		}
	}

	if (n == sizeof(clkid) / sizeof(clkid[0])) {
		pr_err(TAG "invalid clkname %s\n", clkname);
		return -EINVAL;
	}

	if ((id << TEE_CLKMGR_TOKEN_ID_SHIFT) &
		(TEE_CLKMGR_TOKEN_TYPE_MASK << TEE_CLKMGR_TOKEN_TYPE_SHIFT)) {
		pr_err(TAG "%s-%d: invalid id\n", clkname, id);
		return -EINVAL;
	}

	if ((h = kmalloc(sizeof(clkmgr_handle_t), GFP_KERNEL)) == NULL) {
		pr_err(TAG "Failed to alloc clkmgr_handle_t\n");
		return -ENOMEM;
	}

	h->token = TEE_CLKMGR_TOKEN((uint32_t) n, (uint32_t) id);
	h->e = e;
	h->d = d;
	h->p0 = p0;
	h->p1 = p1;
	h->p2 = p2;
	h->argnum = argnum;

	spin_lock(&clk_list_lock);

	/* check for duplication */
	list_for_each_entry(w, &clk_list, le) {
		if (w->token == h->token) {
			pr_err(TAG "clk 0x%x already registerred\n", h->token);
			spin_unlock(&clk_list_lock);
			return -EINVAL;
		}
	}

	list_add(&(h->le), &clk_list);
	spin_unlock(&clk_list_lock);

	return 0;
}

EXPORT_SYMBOL(tee_clkmgr_register);

int tee_clkmgr_init(void)
{
	spin_lock_init(&clk_list_lock);
	return 0;
}

void tee_clkmgr_exit(void)
{
	clkmgr_handle_t *h, *n;
	spin_lock(&clk_list_lock);

	list_for_each_entry_safe(h, n, &clk_list, le) {
		list_del(&(h->le));
		kfree(h);
	}

	spin_unlock(&clk_list_lock);
}
