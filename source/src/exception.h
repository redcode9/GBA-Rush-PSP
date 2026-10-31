#ifdef __cplusplus
extern "C" {
#endif

/* Crash screen; see exception.c. */
void initExceptionHandler(void);
void ExceptionHandler(PspDebugRegBlock *regs);

#ifdef __cplusplus
}
#endif

