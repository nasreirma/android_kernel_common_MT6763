#ifndef _TEE_KERNEL_LOWLEVEL_API_H
#define _TEE_KERNEL_LOWLEVEL_API_H

#ifdef CONFIG_ARM
struct smc_param {
	uint32_t a0;
	uint32_t a1;
	uint32_t a2;
	uint32_t a3;
	uint32_t a4;
	uint32_t a5;
	uint32_t a6;
	uint32_t a7;
};
#endif

#ifdef CONFIG_ARM64
struct smc_param {
	uint64_t a0;
	uint64_t a1;
	uint64_t a2;
	uint64_t a3;
	uint64_t a4;
	uint64_t a5;
	uint64_t a6;
	uint64_t a7;
};
#endif

void tee_smc_call(struct smc_param *param);

#endif
