#ifndef TEE_TA_MGMT_H
#define TEE_TA_MGMT_H

struct tee;
struct tee_ta_inst_desc;

int tee_install_sys_ta(struct tee *tee, struct tee_ta_inst_desc *tee_ta_inst_desc);

#endif
