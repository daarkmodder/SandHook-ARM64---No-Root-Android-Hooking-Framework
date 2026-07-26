#include "../internal/sandhook_internal.h"

namespace sandhook {
namespace arm64 {

enum class Kind { Unknown, B, BL, BR, BLR, RET, ADR, ADRP, LDR_LIT, MOVZ, MOVK, NOP, CBZ, CBNZ, TBZ, TBNZ, Other };

static inline Kind decode_kind(std::uint32_t insn) {
    if (insn == 0xD503201F) return Kind::NOP;
    if ((insn & 0x7E000000) == 0x34000000) return ((insn & 0x01000000) == 0) ? Kind::CBZ : Kind::CBNZ;
    if ((insn & 0x7E000000) == 0x36000000) return ((insn & 0x01000000) == 0) ? Kind::TBZ : Kind::TBNZ;
    if ((insn & 0xFC000000) == 0x14000000) return Kind::B;
    if ((insn & 0xFC000000) == 0x94000000) return Kind::BL;
    if ((insn & 0xFC000000) == 0xD4000000) {
        if ((insn & 0xFFFFFC1F) == 0xD65F0000) return Kind::RET;
        if ((insn & 0xFFFFFC1F) == 0xD61F0000) return Kind::BR;
        if ((insn & 0xFFFFFC1F) == 0xD63F0000) return Kind::BLR;
        return Kind::Other;
    }
    if ((insn & 0x9F000000) == 0x10000000) return Kind::ADR;
    if ((insn & 0x9F000000) == 0x90000000) return Kind::ADRP;
    if ((insn & 0xFF000000) == 0x58000000) return Kind::LDR_LIT;
    if ((insn & 0xFF800000) == 0xD2800000) return Kind::MOVZ;
    if ((insn & 0xFF800000) == 0xF2800000) return Kind::MOVK;
    return Kind::Unknown;
}

// Renombrado Asm -> Inst
void Inst::emit_movz_movk_br(std::uint8_t* out, Address target) {
    auto emit32 = [&](std::uint32_t w) { std::memcpy(out, &w, 4); out += 4; };
    emit32(0xD2800000 | (16 & 31) | ((target & 0xFFFFULL) << 5));
    emit32(0xF2800000 | (16 & 31) | (((target >> 16) & 0xFFFFULL) << 5) | (1u << 21));
    emit32(0xF2800000 | (16 & 31) | (((target >> 32) & 0xFFFFULL) << 5) | (2u << 21));
    emit32(0xF2800000 | (16 & 31) | (((target >> 48) & 0xFFFFULL) << 5) | (3u << 21));
    emit32(0xD61F0000 | ((16 & 31) << 5));
}

void Inst::fill_nops(std::uint8_t* out, std::size_t start, std::size_t end) {
    for (std::size_t off = start; off < end; off += 4) { std::uint32_t nop = 0xD503201F; std::memcpy(out + off, &nop, 4); }
}

} // namespace arm64

// FIX: Acepta max_size para no sobrepasar el tamaño del símbolo
Relocator::Result Relocator::relocate(void* src, void* tramp, std::size_t min_patch, std::size_t max_size) {
    Result r{}; auto* s = reinterpret_cast<std::uint8_t*>(src); auto* t = reinterpret_cast<std::uint8_t*>(tramp);
    Address src_addr = reinterpret_cast<Address>(src); Address tramp_addr = reinterpret_cast<Address>(tramp);
    std::size_t copied = 0, tramp_offset = 0;

    auto emit_abs_jump = [&](Address target, bool is_bl) {
        if ((tramp_offset % 8) != 0) { std::uint32_t nop = 0xD503201F; std::memcpy(t + tramp_offset, &nop, 4); tramp_offset += 4; }
        std::uint32_t ldr_x16 = 0x58000050; std::uint32_t br_x16 = is_bl ? 0xD63F0200 : 0xD61F0200;
        std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4; std::memcpy(t + tramp_offset, &br_x16, 4); tramp_offset += 4;
        std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
    };
    auto emit_cond_abs_jump = [&](std::uint32_t insn, int imm_shift, int imm_mask, Address target) {
        if ((tramp_offset % 8) != 0) { std::uint32_t nop = 0xD503201F; std::memcpy(t + tramp_offset, &nop, 4); tramp_offset += 4; }
        std::uint32_t inv_insn = insn ^ (1 << 24); std::uint32_t new_imm = 5; 
        inv_insn = (inv_insn & ~(imm_mask << imm_shift)) | ((new_imm & imm_mask) << imm_shift);
        std::memcpy(t + tramp_offset, &inv_insn, 4); tramp_offset += 4;
        std::uint32_t ldr_x16 = 0x58000050; std::uint32_t br_x16 = 0xD61F0200;
        std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4; std::memcpy(t + tramp_offset, &br_x16, 4); tramp_offset += 4;
        std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
    };
    auto emit_ldr_lit_abs = [&](std::uint32_t insn, Address target) {
        int rt = insn & 0x1F; if ((tramp_offset % 8) != 0) { std::uint32_t nop = 0xD503201F; std::memcpy(t + tramp_offset, &nop, 4); tramp_offset += 4; }
        std::uint32_t ldr_x16 = 0x58000050; std::uint32_t ldr_rt = 0xF9400200 | rt;
        std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4; std::memcpy(t + tramp_offset, &ldr_rt, 4); tramp_offset += 4;
        std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
    };

    {
        SigGuard guard(src_addr, src_addr + max_size);
        if (guard.jumped()) { r.error = HOOK_INVALID_TARGET; return r; }
        while (copied < min_patch) {
            // FIX: Usar max_size en lugar de kMaxPrologueSize
            if (copied + 4 > max_size) { r.error = HOOK_BOUNDS_EXCEEDED; return r; }
            std::uint32_t insn; std::memcpy(&insn, s + copied, 4);
            auto kind = arm64::decode_kind(insn);
            
            if (kind == arm64::Kind::Unknown) {
                LOGE("[Relocator] Instrucción no soportada (0x%08x) en offset %zu. Falló el hook.", insn, copied);
                r.error = HOOK_RELOCATION_FAILED; 
                return r; 
            }

            std::uint32_t reloced = insn; 
            Address insn_addr = src_addr + copied;

            if (kind == arm64::Kind::ADRP) {
                std::int64_t immlo = (insn >> 29) & 0x3; std::int64_t immhi = (insn >> 5) & 0x7FFFF; std::int64_t imm21 = (immhi << 2) | immlo;
                if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21); Address orig_target = (insn_addr & ~0xFFFULL) + (imm21 << 12);
                Address new_adrp_base = (tramp_addr + tramp_offset) & ~0xFFFULL; std::int32_t new_imm21 = static_cast<std::int32_t>((int64_t(orig_target) - int64_t(new_adrp_base)) >> 12);
                reloced = (insn & 0x9F00001F) | (((new_imm21 >> 2) & 0x7FFFF) << 5) | ((new_imm21 & 0x3) << 29);
                std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4;
            } else if (kind == arm64::Kind::ADR) {
                std::int64_t immlo = (insn >> 29) & 0x3; std::int64_t immhi = (insn >> 5) & 0x7FFFF; std::int64_t imm21 = (immhi << 2) | immlo;
                if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21); Address orig_target = insn_addr + imm21; Address new_adr_base = tramp_addr + tramp_offset;
                std::int32_t new_imm21 = static_cast<std::int32_t>(int64_t(orig_target) - int64_t(new_adr_base));
                reloced = (insn & 0x9F00001F) | (((new_imm21 >> 2) & 0x7FFFF) << 5) | ((new_imm21 & 0x3) << 29);
                std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4;
            } else if (kind == arm64::Kind::LDR_LIT) {
                std::int64_t imm19 = static_cast<std::int64_t>((insn >> 5) & 0x7FFFF); if (imm19 & (1LL << 18)) imm19 |= -(1LL << 19);
                Address orig_target = insn_addr + (imm19 << 2); Address new_base = tramp_addr + tramp_offset; std::int64_t new_offset = int64_t(orig_target) - int64_t(new_base);
                if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) { emit_ldr_lit_abs(insn, orig_target); } 
                else { std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF; reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5); std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4; }
            } else if (kind == arm64::Kind::CBZ || kind == arm64::Kind::CBNZ) {
                std::int64_t imm19 = static_cast<std::int64_t>((insn >> 5) & 0x7FFFF); if (imm19 & (1LL << 18)) imm19 |= -(1LL << 19);
                Address orig_target = insn_addr + (imm19 << 2); Address new_base = tramp_addr + tramp_offset; std::int64_t new_offset = int64_t(orig_target) - int64_t(new_base);
                if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) { emit_cond_abs_jump(insn, 5, 0x7FFFF, orig_target); } 
                else { std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF; reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5); std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4; }
            } else if (kind == arm64::Kind::TBZ || kind == arm64::Kind::TBNZ) {
                std::int64_t imm14 = static_cast<std::int64_t>((insn >> 5) & 0x3FFF); if (imm14 & (1LL << 13)) imm14 |= -(1LL << 14);
                Address orig_target = insn_addr + (imm14 << 2); Address new_base = tramp_addr + tramp_offset; std::int64_t new_offset = int64_t(orig_target) - int64_t(new_base);
                if (new_offset % 4 != 0 || new_offset < -(1LL << 15) || new_offset >= (1LL << 15)) { emit_cond_abs_jump(insn, 5, 0x3FFF, orig_target); } 
                else { std::int32_t new_imm14 = static_cast<std::int32_t>(new_offset >> 2) & 0x3FFF; reloced = (insn & ~(0x3FFF << 5)) | ((new_imm14 & 0x3FFF) << 5); std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4; }
            } else if (kind == arm64::Kind::B || kind == arm64::Kind::BL) {
                std::int64_t imm26 = static_cast<std::int64_t>(insn & 0x03FFFFFF); if (imm26 & (1LL << 25)) imm26 |= -(1LL << 26);
                Address target_addr = insn_addr + (imm26 << 2); emit_abs_jump(target_addr, kind == arm64::Kind::BL);
            } else { std::memcpy(t + tramp_offset, &insn, 4); tramp_offset += 4; }
            copied += 4;
        }
    }
    arm64::Inst::emit_movz_movk_br(t + tramp_offset, src_addr + copied);
    r.copied = copied; r.tramp_size = tramp_offset + 20; r.error = HOOK_OK; return r;
}

} // namespace sandhook