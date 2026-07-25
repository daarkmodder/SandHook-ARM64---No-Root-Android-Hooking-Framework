#include "../internal/sandhook_internal.h"
#include <dlfcn.h>

namespace sandhook {

static bool got_hook_module(void* target, void* replacement, struct dl_phdr_info* info, std::vector<GOTHookEntry>& entries) {
    uintptr_t load_bias = info->dlpi_addr; ElfW(Dyn)* dynamic = nullptr;
    for (size_t i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) { dynamic = (ElfW(Dyn)*)(load_bias + info->dlpi_phdr[i].p_vaddr); break; }
    }
    if (!dynamic) return false;

    ElfW(Rela)* jmprel = nullptr; ElfW(Rela)* reladata = nullptr; size_t pltrelsz = 0; size_t relasz = 0;
    ElfW(Sym)* dynsym = nullptr; const char* strtab = nullptr;

    for (ElfW(Dyn)* d = dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_JMPREL:   jmprel = (ElfW(Rela)*)(load_bias + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_RELA:     reladata = (ElfW(Rela)*)(load_bias + d->d_un.d_ptr); break;
            case DT_RELASZ:   relasz = d->d_un.d_val; break;
            case DT_SYMTAB:   dynsym = (ElfW(Sym)*)(load_bias + d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab = (const char*)(load_bias + d->d_un.d_ptr); break;
        }
    }
    if (!dynsym || !strtab) return false; bool any = false;

    auto process_rela_table = [&](ElfW(Rela)* table, size_t table_size, bool is_jmp_slot) {
        if (!table || table_size == 0) return; size_t count = table_size / sizeof(ElfW(Rela));
        for (size_t i = 0; i < count; i++) {
            ElfW(Rela)* rel = &table[i]; uint32_t r_type = ELF64_R_TYPE(rel->r_info);
            bool valid_type = is_jmp_slot ? (r_type == R_AARCH64_JUMP_SLOT) : (r_type == R_AARCH64_GLOB_DAT);
            if (!valid_type) continue; size_t sym_idx = ELF64_R_SYM(rel->r_info); ElfW(Sym)* sym = &dynsym[sym_idx];
            void* got_entry_addr = (void*)(load_bias + rel->r_offset); void* current_val = nullptr;
            { SigGuard guard((uintptr_t)got_entry_addr, (uintptr_t)got_entry_addr + sizeof(void*)); if (guard.jumped()) continue; std::memcpy(&current_val, got_entry_addr, sizeof(void*)); }
            bool match = false;
            if (current_val == target) match = true;
            else if (sym->st_shndx == SHN_UNDEF) { if (dlsym(RTLD_DEFAULT, strtab + sym->st_name) == target) match = true; }
            else { if ((void*)(load_bias + sym->st_value) == target) match = true; }
            if (!match) continue;
            if (Mem::make_rw(got_entry_addr, sizeof(void*))) {
                void* orig = nullptr; std::memcpy(&orig, got_entry_addr, sizeof(void*)); std::memcpy(got_entry_addr, &replacement, sizeof(void*));
                Mem::flush_caches(got_entry_addr, sizeof(void*)); entries.push_back({(void**)got_entry_addr, orig}); any = true;
            }
        }
    };
    process_rela_table(jmprel, pltrelsz, true); process_rela_table(reladata, relasz, false);
    return any;
}

struct GOTHookCtx { void* target; void* replacement; std::vector<GOTHookEntry>* entries; };
static int got_hook_iterate_cb(struct dl_phdr_info* info, size_t size, void* arg) {
    (void)size; GOTHookCtx* ctx = (GOTHookCtx*)arg; got_hook_module(ctx->target, ctx->replacement, info, *ctx->entries); return 0;
}
bool got_hook_all_modules(void* target, void* replacement, std::vector<GOTHookEntry>& entries) {
    GOTHookCtx ctx = {target, replacement, &entries}; xdl_iterate_phdr(got_hook_iterate_cb, &ctx, XDL_DEFAULT); return !entries.empty();
}

} // namespace sandhook