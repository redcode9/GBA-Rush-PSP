#ifdef __cplusplus
extern "C" {
#endif

/* Crash screen; see exception.c. */
void initExceptionHandler(void);
/* Called by exception.prx on a crash: shows the registers; X also writes
   them to <main_path>tempgba_crash.log. X or O then exits. */
void ExceptionHandler(PspDebugRegBlock *regs);

#ifdef __cplusplus
}
#endif

