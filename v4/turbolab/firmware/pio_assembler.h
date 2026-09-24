// pio_assembler.h — Runtime PIO assembler for RP2040 and RP2350
// Enables dynamic assembly, tuning of cycle delays, and side-set phase scheduling.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

#if defined(__has_include) && __has_include("hardware/pio.h")
#define PIO_ASSEMBLER_ON_DEVICE 1
#include "hardware/pio.h"
#else
#define PIO_ASSEMBLER_ON_DEVICE 0
struct pio_program {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin;
    unsigned int pio_version;
};
typedef void* PIO;
typedef unsigned int uint;
struct pio_sm_config {
    uint32_t fctrl;
    uint32_t shiftctrl;
    uint32_t pinctrl;
    uint32_t execctrl;
};
#endif

class PioAssembler {
public:
    // PIO Registers
    enum Reg {
        PINS = 0,
        X = 1,
        Y = 2,
        ZERO = 3,       // PIO NULL/ZERO register
        PINDIRS = 4,
        EXEC = 4,
        PC = 5,
        STATUS = 5,
        ISR = 6,
        OSR = 7,
        NULL_REG = ZERO,
    };

    struct MovSource {
        Reg reg;
        uint8_t op = 0; // 0 = none, 1 = invert (~), 2 = bit-reverse (::)
    };

    static constexpr MovSource INV_ZERO = { ZERO, 1 };
    static constexpr MovSource INV_NULL = { ZERO, 1 };

    friend constexpr MovSource operator~(Reg r) {
        return MovSource{ r, 1 };
    }

    enum JmpCond {
        ALWAYS = 0,
        NOT_X = 1,
        X_DECR = 2,
        NOT_Y = 3,
        Y_DECR = 4,
        X_NE_Y = 5,
        PIN = 6,
        NOT_OSRE = 7
    };

    enum BlockMode {
        BLOCK = 1,
        NOBLOCK = 0
    };

    enum WaitSource {
        WAIT_GPIO = 0,
        WAIT_PIN = 1,
        WAIT_IRQ = 2
    };

    struct Label {
        int id = -1;
        const char* name = nullptr;
    };

    struct TimePoint {
        Label label;
        int cycle_offset = 0;
    };

    struct SideSetRule {
        TimePoint time_point;
        unsigned side_value = 0;
    };

    friend TimePoint operator+(Label l, int cycles) {
        return TimePoint{ l, cycles };
    }

    static SideSetRule SideSetStarting(TimePoint tp, unsigned side_val) {
        return SideSetRule{ tp, side_val };
    }

private:
    enum IrType {
        IR_INST,
        IR_LABEL,
        IR_DELAY,
        IR_WRAP_TARGET,
        IR_WRAP
    };

    struct IrItem {
        IrType type;
        uint16_t base_inst = 0;
        int label_id = -1;
        const char* label_name = nullptr;
        int delay_cycles = 0;
        int inst_delay = 0;
        int side_override = -1;
        JmpCond jmp_cond = ALWAYS;
        int jmp_target_label = -1;
        std::string comment;
    };

    struct PhysicalInst {
        uint16_t raw_inst = 0;
        int delay = 0;
        unsigned sideset = 0;
        int jmp_target_label = -1;
        int bound_label_id = -1;
        bool is_wrap_target = false;
        bool is_wrap = false;
        std::string comment;
    };

    uint sideset_bits_ = 0;
    bool sideset_opt_ = false;
    bool sideset_pindirs_ = false;
    int max_delay_ = 31;
    bool verbose_ = true;

    int next_label_id_ = 0;
    std::vector<IrItem> ir_items_;
    std::vector<SideSetRule> sideset_rules_;
    std::map<int, const char*> declared_labels_;

    std::vector<uint16_t> compiled_instructions_;
    std::vector<PhysicalInst> physical_instructions_;
    int wrap_target_idx_ = -1;
    int wrap_idx_ = -1;
    struct pio_program program_ = {};

public:
    explicit PioAssembler(uint sideset_bits = 0, bool opt = false, bool pindirs = false)
        : sideset_bits_(sideset_bits), sideset_opt_(opt), sideset_pindirs_(pindirs) {
        int delay_bits = 5 - sideset_bits - (opt ? 1 : 0);
        max_delay_ = (1 << delay_bits) - 1;
    }

    void set_verbose(bool v) { verbose_ = v; }

    Label forward_reference(const char* name = nullptr) {
        Label l;
        l.id = next_label_id_++;
        l.name = name;
        declared_labels_[l.id] = name ? name : "unnamed";
        return l;
    }

    Label create_label(const char* name = nullptr) {
        return forward_reference(name);
    }

    PioAssembler& label(Label l) {
        IrItem item;
        item.type = IR_LABEL;
        item.label_id = l.id;
        item.label_name = l.name;
        ir_items_.push_back(item);
        return *this;
    }

    PioAssembler& wrap_target() {
        IrItem item;
        item.type = IR_WRAP_TARGET;
        ir_items_.push_back(item);
        return *this;
    }

    PioAssembler& wrap() {
        IrItem item;
        item.type = IR_WRAP;
        ir_items_.push_back(item);
        return *this;
    }

    PioAssembler& operator[](int delay_cycles) {
        IrItem item;
        item.type = IR_DELAY;
        item.delay_cycles = delay_cycles;
        ir_items_.push_back(item);
        return *this;
    }

    PioAssembler& operator<<(const SideSetRule& rule) {
        sideset_rules_.push_back(rule);
        return *this;
    }

    // --- Chaining InstBuilder proxy ---
    class InstBuilder {
        PioAssembler* assem_;
        size_t idx_;
    public:
        InstBuilder(PioAssembler* a, size_t idx) : assem_(a), idx_(idx) {}

        operator PioAssembler&() { return *assem_; }

        InstBuilder& delay(uint d) {
            assem_->ir_items_[idx_].inst_delay = d;
            return *this;
        }

        InstBuilder& side(uint s) {
            assem_->ir_items_[idx_].side_override = s;
            return *this;
        }

        // Push / Pull modifiers
        InstBuilder& block() {
            assem_->ir_items_[idx_].base_inst |= (1 << 5);
            return *this;
        }
        InstBuilder& noblock() {
            assem_->ir_items_[idx_].base_inst &= ~(1 << 5);
            return *this;
        }
        InstBuilder& iffull() {
            assem_->ir_items_[idx_].base_inst |= (1 << 6);
            return *this;
        }
        InstBuilder& ifempty() {
            assem_->ir_items_[idx_].base_inst |= (1 << 6);
            return *this;
        }

        // Methods to continue chaining
        InstBuilder pull(BlockMode bm = BLOCK) { return assem_->pull(bm); }
        InstBuilder push(BlockMode bm = BLOCK) { return assem_->push(bm); }
        InstBuilder in(Reg src, uint bit_count) { return assem_->in(src, bit_count); }
        InstBuilder out(Reg dest, uint bit_count) { return assem_->out(dest, bit_count); }
        InstBuilder mov(Reg dest, MovSource src) { return assem_->mov(dest, src); }
        InstBuilder mov(Reg dest, Reg src) { return assem_->mov(dest, src); }
        InstBuilder jmp(JmpCond cond, Label target) { return assem_->jmp(cond, target); }
        InstBuilder jmp(Label target) { return assem_->jmp(target); }
        InstBuilder nop() { return assem_->nop(); }
        PioAssembler& label(Label l) { return assem_->label(l); }
        PioAssembler& wrap_target() { return assem_->wrap_target(); }
        PioAssembler& wrap() { return assem_->wrap(); }
        PioAssembler& operator[](int delay_cycles) { return (*assem_)[delay_cycles]; }
    };

    InstBuilder pull(BlockMode bm = BLOCK) {
        IrItem item;
        item.type = IR_INST;
        item.base_inst = 0x8080 | (bm == BLOCK ? (1 << 5) : 0);
        item.comment = (bm == BLOCK) ? "pull block" : "pull noblock";
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder push(BlockMode bm = BLOCK) {
        IrItem item;
        item.type = IR_INST;
        item.base_inst = 0x8000 | (bm == BLOCK ? (1 << 5) : 0);
        item.comment = (bm == BLOCK) ? "push block" : "push noblock";
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder in(Reg src, uint bit_count) {
        IrItem item;
        item.type = IR_INST;
        uint8_t cnt = (bit_count == 32) ? 0 : (bit_count & 0x1F);
        item.base_inst = 0x4000 | ((src & 0x7) << 5) | cnt;
        item.comment = "in " + reg_name(src) + ", " + std::to_string(bit_count);
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder out(Reg dest, uint bit_count) {
        IrItem item;
        item.type = IR_INST;
        uint8_t cnt = (bit_count == 32) ? 0 : (bit_count & 0x1F);
        item.base_inst = 0x6000 | ((dest & 0x7) << 5) | cnt;
        item.comment = "out " + reg_name(dest) + ", " + std::to_string(bit_count);
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder mov(Reg dest, MovSource src) {
        IrItem item;
        item.type = IR_INST;
        item.base_inst = 0xa000 | ((dest & 0x7) << 5) | ((src.op & 0x3) << 3) | (src.reg & 0x7);
        std::string src_str = reg_name(src.reg);
        if (src.op == 1) src_str = "~" + src_str;
        else if (src.op == 2) src_str = "::" + src_str;
        item.comment = "mov " + reg_name(dest) + ", " + src_str;
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder mov(Reg dest, Reg src) {
        return mov(dest, MovSource{ src, 0 });
    }

    InstBuilder nop() {
        return mov(Y, Y);
    }

    InstBuilder nop(uint delay_cycles) {
        auto b = mov(Y, Y);
        b.delay(delay_cycles);
        return b;
    }

    InstBuilder jmp(JmpCond cond, Label target) {
        IrItem item;
        item.type = IR_INST;
        item.base_inst = 0x0000 | ((cond & 0x7) << 5);
        item.jmp_cond = cond;
        item.jmp_target_label = target.id;
        item.comment = std::string("jmp ") + jmp_cond_name(cond);
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder jmp(Label target) {
        return jmp(ALWAYS, target);
    }

    InstBuilder wait(uint pol, WaitSource src, uint index) {
        IrItem item;
        item.type = IR_INST;
        item.base_inst = 0x2000 | ((pol & 1) << 7) | ((src & 3) << 5) | (index & 0x1F);
        item.comment = "wait";
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder set(Reg dest, uint data) {
        IrItem item;
        item.type = IR_INST;
        item.base_inst = 0xe000 | ((dest & 0x7) << 5) | (data & 0x1F);
        item.comment = "set " + reg_name(dest) + ", " + std::to_string(data);
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    InstBuilder irq(uint irq_num) {
        IrItem item;
        item.type = IR_INST;
        item.base_inst = 0xc000 | (irq_num & 0x1F);
        item.comment = "irq " + std::to_string(irq_num);
        ir_items_.push_back(item);
        return InstBuilder(this, ir_items_.size() - 1);
    }

    // --- Two-Pass Code Generation & Finishing ---
    bool finish() {
        if (verbose_) {
            printf("\n========================================================\n");
            printf("=== PioAssembler: Starting Two-Pass Assembly        ===\n");
            printf("========================================================\n");
            printf("Config: sideset_bits=%u, sideset_opt=%s, max_delay=%d\n",
                   sideset_bits_, sideset_opt_ ? "true" : "false", max_delay_);
        }

        physical_instructions_.clear();
        compiled_instructions_.clear();
        wrap_target_idx_ = -1;
        wrap_idx_ = -1;

        std::map<int, int> label_to_phys_idx;
        int active_label_id = -1;
        int cycle_in_label = 0;

        auto get_sideset_at = [&](int label_id, int cycle) -> unsigned {
            unsigned val = 0;
            int best_offset = -1;
            for (const auto& rule : sideset_rules_) {
                if (rule.time_point.label.id == label_id) {
                    if (rule.time_point.cycle_offset <= cycle && rule.time_point.cycle_offset > best_offset) {
                        best_offset = rule.time_point.cycle_offset;
                        val = rule.side_value;
                    }
                }
            }
            return val;
        };

        auto next_sideset_change = [&](int label_id, int cycle) -> int {
            int next_t = 1000000;
            for (const auto& rule : sideset_rules_) {
                if (rule.time_point.label.id == label_id) {
                    if (rule.time_point.cycle_offset > cycle && rule.time_point.cycle_offset < next_t) {
                        next_t = rule.time_point.cycle_offset;
                    }
                }
            }
            return next_t;
        };

        // PASS 1: Schedule instructions, decompose delays, insert NOPs
        for (size_t i = 0; i < ir_items_.size(); ++i) {
            const auto& item = ir_items_[i];

            if (item.type == IR_LABEL) {
                active_label_id = item.label_id;
                cycle_in_label = 0;
                if (label_to_phys_idx.find(item.label_id) != label_to_phys_idx.end()) {
                    print_error("Duplicate label definition for label ID %d (%s)!",
                                item.label_id, item.label_name ? item.label_name : "unnamed");
                    return false;
                }
                label_to_phys_idx[item.label_id] = static_cast<int>(physical_instructions_.size());
                if (verbose_) {
                    printf("Label bound: id=%d ('%s') -> phys_addr=%zu\n",
                           item.label_id, item.label_name ? item.label_name : "unnamed",
                           physical_instructions_.size());
                }
            } else if (item.type == IR_WRAP_TARGET) {
                wrap_target_idx_ = static_cast<int>(physical_instructions_.size());
                if (verbose_) {
                    printf(".wrap_target set at phys_addr=%d\n", wrap_target_idx_);
                }
            } else if (item.type == IR_WRAP) {
                wrap_idx_ = static_cast<int>(physical_instructions_.size()) - 1;
                if (verbose_) {
                    printf(".wrap set at phys_addr=%d\n", wrap_idx_);
                }
            } else if (item.type == IR_INST) {
                unsigned side = (item.side_override >= 0) 
                              ? static_cast<unsigned>(item.side_override) 
                              : get_sideset_at(active_label_id, cycle_in_label);

                PhysicalInst pi;
                pi.raw_inst = item.base_inst;
                pi.delay = item.inst_delay;
                pi.sideset = side;
                pi.jmp_target_label = item.jmp_target_label;
                pi.comment = item.comment;
                physical_instructions_.push_back(pi);

                cycle_in_label += 1 + item.inst_delay;
            } else if (item.type == IR_DELAY) {
                int remaining_delay = item.delay_cycles;
                if (remaining_delay < 0) {
                    print_error("Negative delay specified: %d cycles!", remaining_delay);
                    return false;
                }

                while (remaining_delay > 0) {
                    unsigned side = get_sideset_at(active_label_id, cycle_in_label);
                    int next_change = next_sideset_change(active_label_id, cycle_in_label);
                    int cycles_in_this_phase = std::min(remaining_delay, next_change - cycle_in_label);
                    if (cycles_in_this_phase <= 0) {
                        cycles_in_this_phase = remaining_delay;
                    }

                    while (cycles_in_this_phase > 0) {
                        int chunk = std::min(cycles_in_this_phase, 1 + max_delay_);
                        int delay = chunk - 1;

                        PhysicalInst nop_inst;
                        nop_inst.raw_inst = 0xa042; // mov y, y (nop)
                        nop_inst.delay = delay;
                        nop_inst.sideset = side;
                        nop_inst.jmp_target_label = -1;
                        nop_inst.comment = "nop";
                        physical_instructions_.push_back(nop_inst);

                        cycle_in_label += chunk;
                        cycles_in_this_phase -= chunk;
                        remaining_delay -= chunk;
                    }
                }
            }
        }

        // PARANOID CHECK: Instruction memory limit
        if (physical_instructions_.size() > 32) {
            print_error("Program length (%zu instructions) EXCEEDS PIO hardware limit of 32 instructions!",
                        physical_instructions_.size());
            return false;
        }

        // PARANOID CHECK: Empty program
        if (physical_instructions_.empty()) {
            print_error("Program contains 0 instructions!");
            return false;
        }

        // PARANOID CHECK: Wrap bounds
        if (wrap_target_idx_ < 0) wrap_target_idx_ = 0;
        if (wrap_idx_ < 0) wrap_idx_ = static_cast<int>(physical_instructions_.size()) - 1;

        if (wrap_target_idx_ >= static_cast<int>(physical_instructions_.size())) {
            print_error("wrap_target (%d) out of bounds [0, %zu)!", wrap_target_idx_, physical_instructions_.size());
            return false;
        }
        if (wrap_idx_ >= static_cast<int>(physical_instructions_.size()) || wrap_idx_ < wrap_target_idx_) {
            print_error("wrap (%d) out of bounds [%d, %zu)!", wrap_idx_, wrap_target_idx_, physical_instructions_.size());
            return false;
        }

        // PASS 2: Patch jump targets and encode sideset/delay bitfields
        compiled_instructions_.clear();
        for (size_t idx = 0; idx < physical_instructions_.size(); ++idx) {
            auto& pi = physical_instructions_[idx];
            uint16_t word = pi.raw_inst;

            // Resolve JMP address
            if (pi.jmp_target_label >= 0) {
                auto it = label_to_phys_idx.find(pi.jmp_target_label);
                if (it == label_to_phys_idx.end()) {
                    print_error("Instruction %zu jumps to UNRESOLVED label ID %d!", idx, pi.jmp_target_label);
                    return false;
                }
                int target_addr = it->second;
                if (target_addr < 0 || target_addr > 31) {
                    print_error("Jump target address %d at instruction %zu out of range [0..31]!", target_addr, idx);
                    return false;
                }
                word |= (target_addr & 0x1F);
                pi.comment += " -> " + std::to_string(target_addr);
            }

            // Encode Delay & Side-set
            if (pi.delay > max_delay_ || pi.delay < 0) {
                print_error("Instruction %zu delay (%d) exceeds max allowed (%d)!", idx, pi.delay, max_delay_);
                return false;
            }
            if (pi.sideset >= (1u << sideset_bits_)) {
                print_error("Instruction %zu sideset (%u) exceeds bitfield width (%u bits)!",
                            idx, pi.sideset, sideset_bits_);
                return false;
            }

            uint8_t delay_val = pi.delay & max_delay_;
            uint8_t side_val = pi.sideset & ((1 << sideset_bits_) - 1);
            int delay_shift = 0;
            int side_shift = 5 - sideset_bits_ - (sideset_opt_ ? 1 : 0);
            uint8_t delay_side_byte = (delay_val << delay_shift);
            if (sideset_opt_) {
                delay_side_byte |= (1 << (5 - 1)); // enable bit
            }
            delay_side_byte |= (side_val << side_shift);

            word |= (static_cast<uint16_t>(delay_side_byte) << 8);
            compiled_instructions_.push_back(word);
        }

        // Setup pio_program struct
        program_.instructions = compiled_instructions_.data();
        program_.length = static_cast<uint8_t>(compiled_instructions_.size());
        program_.origin = -1;
        program_.pio_version = 0;

        if (verbose_) {
            print_disassembly(label_to_phys_idx);
        }

        return true;
    }

    void print_disassembly(const std::map<int, int>& label_map = {}) const {
        printf("\n--- PIO Disassembly (%zu instructions, wrap: %d -> %d) ---\n",
               compiled_instructions_.size(), wrap_target_idx_, wrap_idx_);

        // Reverse map label addresses to names
        std::map<int, std::vector<std::string>> addr_to_labels;
        for (const auto& pair : label_map) {
            auto name_it = declared_labels_.find(pair.first);
            std::string name = (name_it != declared_labels_.end()) ? name_it->second : ("L" + std::to_string(pair.first));
            addr_to_labels[pair.second].push_back(name);
        }

        for (size_t i = 0; i < compiled_instructions_.size(); ++i) {
            auto it = addr_to_labels.find(static_cast<int>(i));
            if (it != addr_to_labels.end()) {
                for (const auto& lbl : it->second) {
                    printf("%s:\n", lbl.c_str());
                }
            }

            if (static_cast<int>(i) == wrap_target_idx_) {
                printf("    .wrap_target\n");
            }

            uint16_t w = compiled_instructions_[i];
            uint side = (w >> (8 + 5 - sideset_bits_ - (sideset_opt_ ? 1 : 0))) & ((1 << sideset_bits_) - 1);
            uint delay = (w >> 8) & max_delay_;
            const auto& pi = physical_instructions_[i];

            printf("  %02zu: 0x%04x    %-18s side %u", i, w, pi.comment.c_str(), side);
            if (delay > 0) {
                printf(" [%u]", delay);
            }
            printf("\n");

            if (static_cast<int>(i) == wrap_idx_) {
                printf("    .wrap\n");
            }
        }
        printf("----------------------------------------------------------\n\n");
    }

    const std::vector<uint16_t>& instructions() const {
        return compiled_instructions_;
    }

    const struct pio_program* program() const {
        return &program_;
    }

    int get_wrap_target() const { return wrap_target_idx_; }
    int get_wrap() const { return wrap_idx_; }

#if PIO_ASSEMBLER_ON_DEVICE
    uint pio_add_program(PIO pio) {
        return ::pio_add_program(pio, &program_);
    }

    pio_sm_config program_get_default_config(uint offset) const {
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, offset + wrap_target_idx_, offset + wrap_idx_);
        sm_config_set_sideset(&c, sideset_bits_, sideset_opt_, sideset_pindirs_);
        return c;
    }
#endif

private:
    static std::string reg_name(Reg r) {
        switch (r) {
            case PINS: return "pins";
            case X: return "x";
            case Y: return "y";
            case ZERO: return "null";
            case PINDIRS: return "pindirs";
            case PC: return "pc";
            case ISR: return "isr";
            case OSR: return "osr";
            default: return "r" + std::to_string(r);
        }
    }

    static const char* jmp_cond_name(JmpCond c) {
        switch (c) {
            case ALWAYS: return "";
            case NOT_X: return "!x,";
            case X_DECR: return "x--,";
            case NOT_Y: return "!y,";
            case Y_DECR: return "y--,";
            case X_NE_Y: return "x!=y,";
            case PIN: return "pin,";
            case NOT_OSRE: return "!osre,";
            default: return "?,";
        }
    }

    void print_error(const char* msg) const {
        printf("\n********************************************************\n");
        printf("* PIO ASSEMBLER FATAL ERROR:\n* %s\n", msg);
        printf("********************************************************\n\n");
    }

    template<typename T, typename... Args>
    void print_error(const char* fmt, T arg1, Args... args) const {
        char buf[256];
        snprintf(buf, sizeof(buf), fmt, arg1, args...);
        printf("\n********************************************************\n");
        printf("* PIO ASSEMBLER FATAL ERROR:\n* %s\n", buf);
        printf("********************************************************\n\n");
    }
};
