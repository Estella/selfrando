/*
 * This file is part of selfrando.
 * Copyright (c) 2015-2017 Immunant Inc.
 * For license information, see the LICENSE file
 * included with selfrando.
 *
 */

#include <OS.h>
#include <TrapInfo.h>
#include "RelocTypes.h"

#include <elf.h>

namespace os {

Module::Relocation::Relocation(const Module &mod, const trap_reloc_t &reloc)
    : m_module(mod), m_orig_src_ptr(mod.address_from_trap(reloc.address).to_ptr()),
      m_src_ptr(mod.address_from_trap(reloc.address).to_ptr()), m_type(reloc.type),
      m_symbol_ptr(mod.address_from_trap(reloc.symbol).to_ptr()), m_addend(reloc.addend) {
    m_has_symbol_ptr = (reloc.symbol != 0); // FIXME: what if zero addresses are legit???
}

template<typename Value>
static inline RANDO_SECTION
uintptr_t page_address(Value val) {
    return reinterpret_cast<uintptr_t>(val) & ~0xfffUL;
}

enum class Instruction : uint32_t {
    MOVW = 0,
    ADR,
    LDST,
    ADD,
    LDLIT,
    TSTBR,
    CONDBR,
    JUMPCALL,
};

static inline RANDO_SECTION
uint32_t read_insn_operand(BytePointer at_ptr, Instruction insn) {
    auto insn_value = *reinterpret_cast<uint32_t*>(at_ptr);
    switch (insn) {
    case Instruction::MOVW:
        return (insn_value & 0x1fffe0) >> 5;  // 16/5
    case Instruction::ADR:
        // ADR is really weird: the lowest two bits of the result
        // are encoded in bits 29-30 of the instruction
        return ((insn_value & 0xffffe0) >> 3) | ((insn_value >> 29) & 3);
    case Instruction::LDST:
    case Instruction::ADD:
        return (insn_value & 0x3ffc00) >> 10; // 12/10
    case Instruction::TSTBR:
        return (insn_value & 0x7ffe0)  >> 5;  // 14/5
    case Instruction::LDLIT:
    case Instruction::CONDBR:
        return (insn_value & 0xffffe0) >> 5;  // 19/5
    case Instruction::JUMPCALL:
        return (insn_value & 0x3ffffff);      // 26/0
    default:
        RANDO_ASSERT(false);
    }
}

template<unsigned bits>
static inline RANDO_SECTION
int32_t sign_extend(uint32_t x) {
    uint32_t sign_bit = 1 << (bits - 1);
    if ((x & sign_bit) != 0)
        x |= ~(sign_bit - 1);
    return *reinterpret_cast<int32_t*>(&x);
}

static inline RANDO_SECTION
bool insn_is_adrp(uint32_t insn) {
    return (insn & 0x9f000000) == 0x90000000;
}

static inline RANDO_SECTION
bool insn_is_ldst(uint32_t insn) {
    return (insn & 0x0a000000) == 0x08000000;
}

static inline RANDO_SECTION
bool insn_is_ldst_uimm(uint32_t insn) {
    return (insn & 0x3b000000) == 0x39000000;
}

static inline RANDO_SECTION
void force_adrp(BytePointer ptr) {
    uint32_t* insn_ptr = reinterpret_cast<uint32_t*>(ptr);
    *insn_ptr &= 0x60ffffff;
    *insn_ptr |= 0x90000000;
}

BytePointer Module::Relocation::get_target_ptr() const {
    // IMPORTANT: Keep TrapInfo/TrapInfoRelocs.h in sync whenever a new
    // relocation requires a symbol and/or addend.

    auto at_ptr = m_src_ptr;
    auto orig_ptr = m_orig_src_ptr;
    switch(m_type) {
    case R_AARCH64_ABS32:
        return reinterpret_cast<BytePointer>(*reinterpret_cast<uint32_t*>(at_ptr));
    case R_AARCH64_ABS64:
        return reinterpret_cast<BytePointer>(*reinterpret_cast<uint64_t*>(at_ptr));
    case R_AARCH64_PREL32:
        return at_ptr + *reinterpret_cast<int32_t*>(at_ptr) - m_addend;
    case R_AARCH64_PREL64:
        return at_ptr + *reinterpret_cast<int64_t*>(at_ptr) - m_addend;
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADR_PREL_PG_HI21_NC:
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC:
        RANDO_ASSERT(m_has_symbol_ptr);
        return m_symbol_ptr.to_ptr();
    case R_AARCH64_LD_PREL_LO19:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_TLSLD_LD_PREL19:
    case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
    case R_AARCH64_TLSDESC_LD_PREL19:
        return orig_ptr + sign_extend<21>(read_insn_operand(at_ptr, Instruction::LDLIT) << 2);
    case R_AARCH64_ADR_PREL_LO21:
    case R_AARCH64_TLSGD_ADR_PREL21:
    case R_AARCH64_TLSLD_ADR_PREL21:
    case R_AARCH64_TLSDESC_ADR_PREL21:
        return orig_ptr + sign_extend<21>(read_insn_operand(at_ptr, Instruction::ADR));
    case R_AARCH64_TSTBR14:
        return orig_ptr + sign_extend<16>(read_insn_operand(at_ptr, Instruction::TSTBR) << 2);
    case R_AARCH64_CONDBR19:
        return orig_ptr + sign_extend<21>(read_insn_operand(at_ptr, Instruction::CONDBR) << 2);
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        return orig_ptr + sign_extend<28>(read_insn_operand(at_ptr, Instruction::JUMPCALL) << 2);
    case R_AARCH64_GOTREL64:
        return m_module.get_got_ptr() + *reinterpret_cast<int64_t*>(at_ptr);
    case R_AARCH64_GOTREL32:
        // We need to use the original address as the source here (not the diversified one)
        // to keep in consistent with the original relocation entry (before shuffling)
        return m_module.get_got_ptr() + *reinterpret_cast<int32_t*>(at_ptr);
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_TLSGD_ADR_PAGE21:
    case R_AARCH64_TLSLD_ADR_PAGE21:
    case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case R_AARCH64_TLSDESC_ADR_PAGE21:
        {
            // We have to account for bfd&gold's fixes for erratum 843419:
            // when an ADRP instruction falls on the last few bytes of a page,
            // the linker replaces it with an ADR
            auto base = reinterpret_cast<uintptr_t>(orig_ptr);
            auto delta = sign_extend<21>(read_insn_operand(at_ptr, Instruction::ADR));
            if (insn_is_adrp(*reinterpret_cast<uint32_t*>(at_ptr))) {
                base = page_address(base);
                delta <<= 12;
            }
            return reinterpret_cast<BytePointer>(base + delta);
        }
    default:
        RANDO_ASSERT(false);
        return nullptr;
    }
}

#define RANDO_ASSERT_DELTA_SIZE(bits, delta)        \
    do {                                            \
        ptrdiff_t max =  (1LL << ((bits) - 1)) - 1; \
        ptrdiff_t min = -(1LL << ((bits) - 1));     \
        RANDO_ASSERT((delta) <= max);               \
        RANDO_ASSERT((delta) >= min);               \
    } while (0)

static inline RANDO_SECTION
void write_insn_operand(BytePointer at_ptr, Instruction insn,
                        ptrdiff_t new_value, size_t shift) {
    auto insn_ptr = reinterpret_cast<uint32_t*>(at_ptr);
    new_value >>= shift;
    switch(insn) {
    case Instruction::MOVW:
        RANDO_ASSERT(false);
#if 0 // FIXME: implement this correctly
        *insn_ptr &= ~0x601fffe0;
        if (new_value < 0) {
          // X < 0 => MOVN instruction, negate bits
          new_value = ~new_value;
        } else {
          // X >= 0 => MOVZ instruction
          *insn_ptr |= (0x2 << 29);
        }
        *insn_ptr |= ((new_value >> shift) & 0xffff) << 5;
        break;
#endif
    case Instruction::ADR:
        RANDO_ASSERT_DELTA_SIZE(21, new_value);
        *insn_ptr &= ~0x60ffffe0;
        *insn_ptr |= ((new_value & 0x3) << 29) |
                     ((new_value & 0x1ffffc) << 3);
        break;
    case Instruction::LDST:
    case Instruction::ADD:
        *insn_ptr &= ~0x3ffc00;
        *insn_ptr |= (new_value & 0xfff) << 10;
        break;
    case Instruction::TSTBR:
        RANDO_ASSERT_DELTA_SIZE(14, new_value);
        *insn_ptr &= ~0x7ffe0;
        *insn_ptr |= (new_value & 0x3fff) << 5;
        break;
    case Instruction::LDLIT:
    case Instruction::CONDBR:
        RANDO_ASSERT_DELTA_SIZE(19, new_value);
        *insn_ptr &= ~0xffffe0;
        *insn_ptr |= (new_value & 0x7ffff) << 5;
        break;
    case Instruction::JUMPCALL:
        RANDO_ASSERT_DELTA_SIZE(26, new_value);
        *insn_ptr &= ~0x3ffffff;
        *insn_ptr |= (new_value & 0x3ffffff);
        break;
    default:
        RANDO_ASSERT(false);
    }
    if ((reinterpret_cast<uintptr_t>(at_ptr) & 0xfff) >= 0xff8 &&
        insn_is_adrp(insn_ptr[0]) && insn_is_ldst(insn_ptr[1]) &&
        (insn_is_ldst_uimm(insn_ptr[2]) || insn_is_ldst_uimm(insn_ptr[3]))) {
        // We may have an 843419 erratum
        API::DebugPrintf<1>("Warning: violating erratum 843419 at %p\n", at_ptr);
#if 0
        RANDO_ASSERT(false);
#endif
    }
}

void Module::Relocation::set_target_ptr(BytePointer new_target) {
    auto at_ptr = m_src_ptr;
    ptrdiff_t        pcrel_delta = new_target - at_ptr;
    ptrdiff_t addend_pcrel_delta = pcrel_delta + m_addend;
    ptrdiff_t        pcrel_page_delta = (page_address(new_target)            - page_address(at_ptr));
    ptrdiff_t addend_pcrel_page_delta = (page_address(new_target + m_addend) - page_address(at_ptr));
    switch(m_type) {
    case R_AARCH64_ABS32:
        *reinterpret_cast<uint32_t*>(at_ptr) = reinterpret_cast<uintptr_t>(new_target);
        break;
    case R_AARCH64_ABS64:
        *reinterpret_cast<uint64_t*>(at_ptr) = reinterpret_cast<uintptr_t>(new_target);
        break;
    case R_AARCH64_PREL32:
        RANDO_ASSERT_DELTA_SIZE(32, addend_pcrel_delta);
        *reinterpret_cast<int32_t*>(at_ptr) = static_cast<int32_t>(addend_pcrel_delta);
        break;
    case R_AARCH64_PREL64:
        *reinterpret_cast<int64_t*>(at_ptr) = static_cast<int64_t>(addend_pcrel_delta);
        break;
    case R_AARCH64_LD_PREL_LO19:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_TLSLD_LD_PREL19:
    case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
    case R_AARCH64_TLSDESC_LD_PREL19:
        write_insn_operand(at_ptr, Instruction::LDLIT, pcrel_delta, 2);
        RANDO_ASSERT(pcrel_delta == sign_extend<21>(read_insn_operand(at_ptr, Instruction::LDLIT) << 2));
        break;
    case R_AARCH64_ADR_PREL_LO21:
    case R_AARCH64_TLSGD_ADR_PREL21:
    case R_AARCH64_TLSLD_ADR_PREL21:
    case R_AARCH64_TLSDESC_ADR_PREL21:
        write_insn_operand(at_ptr, Instruction::ADR, pcrel_delta, 0);
        RANDO_ASSERT(pcrel_delta == sign_extend<21>(read_insn_operand(at_ptr, Instruction::ADR)));
        break;
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADR_PREL_PG_HI21_NC:
        RANDO_ASSERT(m_has_symbol_ptr);
        write_insn_operand(at_ptr, Instruction::ADR, addend_pcrel_page_delta, 12);
        RANDO_ASSERT(addend_pcrel_page_delta == (sign_extend<21>(read_insn_operand(at_ptr, Instruction::ADR)) << 12));
        break;
    case R_AARCH64_ADD_ABS_LO12_NC:
        write_insn_operand(at_ptr, Instruction::ADD, reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff, 0);
        RANDO_ASSERT((reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff) ==
                     read_insn_operand(at_ptr, Instruction::ADD));
        break;
    case R_AARCH64_LDST8_ABS_LO12_NC:
        RANDO_ASSERT(m_has_symbol_ptr);
        write_insn_operand(at_ptr, Instruction::LDST, reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff, 0);
        RANDO_ASSERT((reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff) ==
                     (read_insn_operand(at_ptr, Instruction::LDST) << 0));
        break;
    case R_AARCH64_LDST16_ABS_LO12_NC:
        RANDO_ASSERT(m_has_symbol_ptr);
        write_insn_operand(at_ptr, Instruction::LDST, reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff, 1);
        RANDO_ASSERT((reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff) ==
                     (read_insn_operand(at_ptr, Instruction::LDST) << 1));
        break;
    case R_AARCH64_LDST32_ABS_LO12_NC:
        RANDO_ASSERT(m_has_symbol_ptr);
        write_insn_operand(at_ptr, Instruction::LDST, reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff, 2);
        RANDO_ASSERT((reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff) ==
                     (read_insn_operand(at_ptr, Instruction::LDST) << 2));
        break;
    case R_AARCH64_LDST64_ABS_LO12_NC:
        RANDO_ASSERT(m_has_symbol_ptr);
        write_insn_operand(at_ptr, Instruction::LDST, reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff, 3);
        RANDO_ASSERT((reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff) ==
                     (read_insn_operand(at_ptr, Instruction::LDST) << 3));
        break;
    case R_AARCH64_LDST128_ABS_LO12_NC:
        RANDO_ASSERT(m_has_symbol_ptr);
        write_insn_operand(at_ptr, Instruction::LDST, reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff, 4);
        RANDO_ASSERT((reinterpret_cast<uint64_t>(new_target + m_addend) & 0xfff) ==
                     (read_insn_operand(at_ptr, Instruction::LDST) << 4));
        break;
    case R_AARCH64_TSTBR14:
        // FIXME: handle overflow (see below)
        write_insn_operand(at_ptr, Instruction::TSTBR, pcrel_delta, 2);
        RANDO_ASSERT(pcrel_delta == sign_extend<16>(read_insn_operand(at_ptr, Instruction::TSTBR) << 2));
        break;
    case R_AARCH64_CONDBR19:
        // FIXME: sometimes, our function shuffling pushes functions
        // out of each other's CONDBR19 range
        write_insn_operand(at_ptr, Instruction::CONDBR, pcrel_delta, 2);
        RANDO_ASSERT(pcrel_delta == sign_extend<21>(read_insn_operand(at_ptr, Instruction::CONDBR) << 2));
        break;
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        write_insn_operand(at_ptr, Instruction::JUMPCALL, pcrel_delta, 2);
        RANDO_ASSERT(pcrel_delta == sign_extend<28>(read_insn_operand(at_ptr, Instruction::JUMPCALL) << 2));
        break;
    case R_AARCH64_GOTREL64:
	*reinterpret_cast<int64_t*>(at_ptr) = static_cast<int64_t>(new_target - m_module.get_got_ptr());
        break;
    case R_AARCH64_GOTREL32:
        RANDO_ASSERT_DELTA_SIZE(32, new_target - m_module.get_got_ptr());
        *reinterpret_cast<int32_t*>(at_ptr) = static_cast<int32_t>(new_target - m_module.get_got_ptr());
        break;
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_TLSGD_ADR_PAGE21:
    case R_AARCH64_TLSLD_ADR_PAGE21:
    case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case R_AARCH64_TLSDESC_ADR_PAGE21:
        // FIXME: should we implement our own fix for 843419???
        // FIXME: for now, we force every ADR into an ADRP
        force_adrp(at_ptr);
        write_insn_operand(at_ptr, Instruction::ADR, pcrel_page_delta, 12);
        RANDO_ASSERT(insn_is_adrp(*reinterpret_cast<uint32_t*>(at_ptr)));
        RANDO_ASSERT(pcrel_page_delta == (sign_extend<21>(read_insn_operand(at_ptr, Instruction::ADR)) << 12));
        break;
    default:
        RANDO_ASSERT(false);
        break;
    }
}

BytePointer Module::Relocation::get_got_entry() const {
    auto at_ptr = m_src_ptr;
    switch(m_type) {
    // TODO: handle arch GOT relocations
    default:
        return nullptr;
    }
}

Module::Relocation::Type Module::Relocation::get_pointer_reloc_type() {
    return R_AARCH64_ABS64;
}

void Module::Relocation::fixup_export_trampoline(BytePointer *export_ptr,
                                                 const Module &module,
                                                 FunctionList *functions) {
    //RANDO_ASSERT(**export_ptr == 0xE9);
    // According to the AArch64 encoding document I found,
    // unconditional branches are encoded as:
    // 000101bb bbbbbbbb bbbbbbbb bbbbbbbb == (5 << 26) | offset
    //RANDO_ASSERT((**export_ptr >> 26) == 0x5);
    //RANDO_ASSERT(**export_ptr == 0xff ||**export_ptr == 0xfe ||**export_ptr == 0x94 || **export_ptr == 0x97 ||
    //             **export_ptr == 0x14 || **export_ptr == 0x17);
    Module::Relocation reloc(module,
                             module.address_from_ptr(*export_ptr),
                             R_AARCH64_JUMP26);
    functions->AdjustRelocation(&reloc);
    *export_ptr += 4;
}

void Module::Relocation::fixup_entry_point(const Module &module,
                                           uintptr_t entry_point,
                                           uintptr_t target) {
    RANDO_ASSERT(*reinterpret_cast<uint32_t*>(entry_point) == 0x14000001);
    Module::Relocation reloc(module,
                             module.address_from_ptr(entry_point),
                             R_AARCH64_JUMP26);
    reloc.set_target_ptr(reinterpret_cast<BytePointer>(target));

    // Flush the icache line containing this entry point
    // FIXME: this might be slow to do twice (once per entry point),
    // we might want to merge both flushes
    Section reloc_section(module,
                          reinterpret_cast<uintptr_t>(reloc.get_source_ptr()),
                          sizeof(uint32_t));
    reloc_section.flush_icache();
}

template<>
size_t Module::arch_reloc_type<Elf64_Rela>(const Elf64_Rela *rel) {
    auto rel_type = ELF64_R_TYPE(rel->r_info);
    if (rel_type == R_AARCH64_RELATIVE ||
        rel_type == R_AARCH64_GLOB_DAT ||
        rel_type == R_AARCH64_ABS64) {
        return R_AARCH64_ABS64;
    }
    return 0;
}

void Module::preprocess_arch() {
    m_arch_relocs = 0;
    build_arch_relocs<Elf64_Dyn, Elf64_Rela, DT_RELA, DT_RELASZ>();
}

void Module::relocate_arch(FunctionList *functions) const {
}

} // namespace os
