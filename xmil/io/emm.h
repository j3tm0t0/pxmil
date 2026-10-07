#ifdef __cplusplus
extern "C" {
#endif

void emm_reset(void);
void IOOUTCALL emm_o(UINT port, REG8 dat);
REG8 IOINPCALL emm_i(UINT port);

#ifdef __cplusplus
}
#endif
