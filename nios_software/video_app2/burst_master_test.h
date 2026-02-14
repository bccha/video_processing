#ifndef BURST_MASTER_TEST_H_
#define BURST_MASTER_TEST_H_

#define OCM_TEST_WORDS 1024         // 4KB OCM-to-DDR
#define DDR_TEST_WORDS (256 * 1024) // 1MB DDR-to-DDR

// CSR Register Offsets
#define REG_CTRL (0 * 4)
#define REG_STATUS (1 * 4)
#define REG_SRC_ADDR (2 * 4)
#define REG_DST_ADDR (3 * 4)
#define REG_LEN (4 * 4)
#define REG_RD_BURST (5 * 4)
#define REG_WR_BURST (6 * 4)
#define REG_COEFF (7 * 4)

void run_ocm_to_ddr_test(unsigned int csr_base, unsigned int ddr_base);
void run_ddr_to_ddr_test(unsigned int csr_base, unsigned int ddr_base);

#endif /* BURST_MASTER_TEST_H_ */
