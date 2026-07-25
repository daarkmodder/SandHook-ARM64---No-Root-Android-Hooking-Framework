#include "../internal/sandhook_internal.h"

// Trick para Termux: gettid existe en libc.so pero no en las cabeceras
extern "C" pid_t gettid();

namespace sandhook {

struct SigHandlerState {
    std::atomic<bool> active{false};
    std::atomic<uintptr_t> range_start{0}, range_end{0};
    std::mutex mu;
    std::unordered_map<pid_t, sigjmp_buf*> thread_jmpbufs;
    std::atomic<int> handler_installed{0};
};

static SigHandlerState g_sig_state;
static struct sigaction g_old_sigact;

static void sandhook_sig_handler(int sig, siginfo_t* info, void* context) {
    (void)sig; (void)context;
    if (g_sig_state.active.load(std::memory_order_acquire)) {
        uintptr_t fault_addr = (uintptr_t)info->si_addr;
        if (fault_addr >= g_sig_state.range_start.load(std::memory_order_relaxed) && 
            fault_addr < g_sig_state.range_end.load(std::memory_order_relaxed)) {
            pid_t tid = gettid();
            std::lock_guard<std::mutex> lk(g_sig_state.mu);
            auto it = g_sig_state.thread_jmpbufs.find(tid);
            if (it != g_sig_state.thread_jmpbufs.end() && it->second) siglongjmp(*it->second, 1);
        }
    }
    if (g_old_sigact.sa_flags & SA_SIGINFO) g_old_sigact.sa_sigaction(sig, info, context);
    else if (g_old_sigact.sa_handler != SIG_IGN && g_old_sigact.sa_handler != SIG_DFL) g_old_sigact.sa_handler(sig);
    else { sigaction(SIGSEGV, &g_old_sigact, nullptr); raise(SIGSEGV); }
}

static void ensure_sig_handler_installed() {
    int expected = 0;
    if (g_sig_state.handler_installed.compare_exchange_strong(expected, 1)) {
        struct sigaction act; memset(&act, 0, sizeof(act));
        act.sa_sigaction = sandhook_sig_handler; act.sa_flags = SA_SIGINFO; sigemptyset(&act.sa_mask);
        sigaction(SIGSEGV, &act, &g_old_sigact);
    }
}

SigGuard::SigGuard(uintptr_t range_start, uintptr_t range_end) {
    ensure_sig_handler_installed();
    if (sigsetjmp(jmpbuf_, 1) == 0) {
        pid_t tid = gettid();
        { std::lock_guard<std::mutex> lk(g_sig_state.mu); g_sig_state.thread_jmpbufs[tid] = &jmpbuf_; }
        g_sig_state.range_start.store(range_start, std::memory_order_relaxed);
        g_sig_state.range_end.store(range_end, std::memory_order_relaxed);
        g_sig_state.active.store(true, std::memory_order_release);
    } else {
        jumped_ = true;
        pid_t tid = gettid();
        std::lock_guard<std::mutex> lk(g_sig_state.mu);
        g_sig_state.thread_jmpbufs.erase(tid);
        g_sig_state.active.store(false, std::memory_order_release);
    }
}

SigGuard::~SigGuard() {
    if (!jumped_) {
        pid_t tid = gettid();
        std::lock_guard<std::mutex> lk(g_sig_state.mu);
        g_sig_state.thread_jmpbufs.erase(tid);
        g_sig_state.active.store(false, std::memory_order_release);
    }
}

} // namespace sandhook