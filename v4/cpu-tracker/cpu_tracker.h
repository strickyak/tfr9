#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include "opcodes_6809.h"
#include "register_state.h"

#ifndef LOG_MISPREDICTS
#define LOG_MISPREDICTS 0
#endif

namespace cputracker {

enum class CycleType : uint8_t {
  READ = 0,
  WRITE = 1,
};

enum class TrackerMode : uint8_t {
  PREDICTING = 0, // Firmware: autonomously predict FICs
  TRAINING = 1,   // Offline Linux: validate predictions against ground truth
};

enum class CpuType : uint8_t {
  M6809 = 0,
  HD6309 = 1,
};

enum class TrackerState : uint8_t {
  RESET_WAIT = 0,
  OPCODE_FETCH,
  PREFIX_PAGE,
  OPERAND_BYTES,
  EXTRA_FETCH,
  STACK_DUMMY_READ,
  DATA_ACCESS,
  BRANCH_TEST,
  RESYNC_HUNT,
};

struct TrackerStats {
  uint64_t total_cycles = 0;
  uint64_t read_cycles = 0;
  uint64_t write_cycles = 0;
  uint64_t fic_ground_truth = 0;
  uint64_t fic_predicted = 0;
  uint64_t true_positives = 0;
  uint64_t false_positives = 0;
  uint64_t false_negatives = 0;
  uint64_t instructions_executed = 0;
  uint64_t resync_events = 0;
  uint64_t mispredict_events = 0;
};

class CpuTracker {
 public:
  CpuTracker() {
    Reset();
  }

  void Reset() {
    state_ = TrackerState::RESET_WAIT;
    mode_ = TrackerMode::PREDICTING;
    cpu_type_ = CpuType::M6809;
    snarfing_enabled_ = true;
    is_synced_ = false;

    regs_.Reset();
    stats_ = TrackerStats{};

    reset_msb_ = 0;
    cur_prefix_ = 0;
    cur_opcode_ = 0;
    cur_op_info_ = OpcodeInfo{};
    cur_instr_start_pc_ = 0;

    operand_bytes_needed_ = 0;
    operand_bytes_read_ = 0;
    operand_buf_[0] = operand_buf_[1] = operand_buf_[2] = operand_buf_[3] = 0;

    data_bytes_needed_ = 0;
    data_bytes_done_ = 0;
    expected_data_write_ = false;

    branch_target_ = 0;
    branch_fallthrough_ = 0;

    indexed_postbyte_ = 0;
    indexed_extra_offset_bytes_ = 0;
    indexed_extra_fetches_needed_ = 0;
    indexed_extra_fetches_done_ = 0;
    indexed_indirect_ = false;
    indexed_indirect_ptr_reads_ = 0;
    indexed_indirect_target_ = 0;

    jsr_active_ = false;
    jsr_target_ = 0;

    pulled_pc_ = 0;
    pull_pc_active_ = false;

    rti_entire_ = false;
    swi_vector_reads_ = 0;
    vector_target_ = 0;

    resync_count_ = 0;
    resync_last_addr_ = 0;
  }

  void SetMode(TrackerMode mode) { mode_ = mode; }
  void SetCpuType(CpuType type) { cpu_type_ = type; }
  void EnableRegisterSnarfing(bool enable) { snarfing_enabled_ = enable; }

  bool IsSynced() const { return is_synced_; }
  bool Is6309NativeMode() const { return regs_.Is6309Native(); }
  uint16_t GetPC() const { return regs_.pc; }
  uint8_t GetCurrentOpcode() const { return cur_opcode_; }
  const TrackerStats& GetStats() const { return stats_; }
  const RegisterState& GetRegisters() const { return regs_; }

  // Primary cycle consumption method
  // Returns true if this cycle was predicted as an FIC
  bool ProcessCycle(CycleType type, uint16_t addr, uint8_t data, bool is_fic_ground_truth = false) {
    stats_.total_cycles++;
    if (type == CycleType::READ) stats_.read_cycles++;
    else stats_.write_cycles++;

    if (is_fic_ground_truth) stats_.fic_ground_truth++;

    bool predicted_fic = false;

    // Filter out $FFFF reads (idle / omitted internal cycles)
    if (addr == 0xFFFF && type == CycleType::READ) {
      UpdateStats(predicted_fic, is_fic_ground_truth);
      return false;
    }

    // Hardware Vector Fetch Hook for Interrupts during RESYNC_HUNT
    if (type == CycleType::READ && addr >= 0xFFF0 && addr <= 0xFFFE && state_ == TrackerState::RESYNC_HUNT) {
      if ((addr & 1) == 0) {
        vector_target_ = (uint16_t(data) << 8);
      } else {
        vector_target_ |= data;
        regs_.pc = vector_target_;
        state_ = TrackerState::OPCODE_FETCH;
        is_synced_ = true;
      }
      UpdateStats(predicted_fic, is_fic_ground_truth);
      return false;
    }

    // Handle repeated/duplicate opcode or prefix read cycle at current instruction start PC
    if (type == CycleType::READ && addr == cur_instr_start_pc_ &&
        ((state_ == TrackerState::OPERAND_BYTES && operand_bytes_read_ == 0) ||
         state_ == TrackerState::PREFIX_PAGE)) {
      predicted_fic = HandleOpcodeFetch(type, addr, data);
      UpdateStats(predicted_fic, is_fic_ground_truth);
      return predicted_fic;
    }

    // Handle pseudo-idle read ($FFFF) during DATA_ACCESS:
    // If a data read was from $FFFF, it was omitted from the log, so the next incoming cycle
    // is already the next instruction's opcode fetch at regs_.pc!
    if (type == CycleType::READ && addr == regs_.pc && state_ == TrackerState::DATA_ACCESS && !expected_data_write_) {
      state_ = TrackerState::OPCODE_FETCH;
    }

    switch (state_) {
      case TrackerState::RESET_WAIT:
        HandleResetWait(type, addr, data);
        break;

      case TrackerState::OPCODE_FETCH:
        predicted_fic = HandleOpcodeFetch(type, addr, data);
        break;

      case TrackerState::PREFIX_PAGE:
        HandlePrefixPage(type, addr, data);
        break;

      case TrackerState::OPERAND_BYTES:
        HandleOperandBytes(type, addr, data);
        break;

      case TrackerState::EXTRA_FETCH:
        HandleExtraFetch(type, addr, data);
        break;

      case TrackerState::STACK_DUMMY_READ:
        HandleStackDummyRead(type, addr, data);
        break;

      case TrackerState::DATA_ACCESS:
        HandleDataAccess(type, addr, data);
        break;

      case TrackerState::BRANCH_TEST:
        predicted_fic = HandleBranchTest(type, addr, data);
        break;

      case TrackerState::RESYNC_HUNT:
        predicted_fic = HandleResyncHunt(type, addr, data);
        break;
    }

    UpdateStats(predicted_fic, is_fic_ground_truth);
    return predicted_fic;
  }

 private:
  TrackerState state_ = TrackerState::RESET_WAIT;
  TrackerMode mode_ = TrackerMode::PREDICTING;
  CpuType cpu_type_ = CpuType::M6809;
  bool snarfing_enabled_ = true;
  bool is_synced_ = false;

  RegisterState regs_;
  TrackerStats stats_;
  static inline OpcodeTable opcode_tables_{};

  // Internal cycle processing fields
  uint8_t reset_msb_ = 0;
  uint8_t cur_prefix_ = 0;
  uint8_t cur_opcode_ = 0;
  OpcodeInfo cur_op_info_{};
  uint16_t cur_instr_start_pc_ = 0;

  uint8_t operand_bytes_needed_ = 0;
  uint8_t operand_bytes_read_ = 0;
  uint8_t operand_buf_[4]{};

  uint8_t data_bytes_needed_ = 0;
  uint8_t data_bytes_done_ = 0;
  bool expected_data_write_ = false;

  uint16_t branch_target_ = 0;
  uint16_t branch_fallthrough_ = 0;

  uint8_t indexed_postbyte_ = 0;
  uint8_t indexed_extra_offset_bytes_ = 0;
  uint8_t indexed_extra_fetches_needed_ = 0;
  uint8_t indexed_extra_fetches_done_ = 0;
  bool indexed_indirect_ = false;
  uint8_t indexed_indirect_ptr_reads_ = 0;
  uint16_t indexed_indirect_target_ = 0;

  bool jsr_active_ = false;
  uint16_t jsr_target_ = 0;

  uint16_t pulled_pc_ = 0;
  bool pull_pc_active_ = false;

  bool rti_entire_ = false;
  uint8_t swi_vector_reads_ = 0;
  uint16_t vector_target_ = 0;

  int resync_count_ = 0;
  uint16_t resync_last_addr_ = 0;

  void UpdateStats(bool predicted, bool ground_truth) {
    if (!is_synced_) return;
    if (predicted) stats_.fic_predicted++;

    if (predicted && ground_truth) stats_.true_positives++;
    else if (predicted && !ground_truth) {
      stats_.false_positives++;
      LogMispredict("False Positive (predicted FIC on non-FIC cycle)");
    } else if (!predicted && ground_truth) {
      stats_.false_negatives++;
      LogMispredict("False Negative (missed ground truth FIC)");
    }
  }

  void LogMispredict(const char* reason) {
    (void)reason;
#if LOG_MISPREDICTS
    stats_.mispredict_events++;
    std::printf("[MISPREDICT #%llu] %s | State: %d\n",
      (unsigned long long)stats_.total_cycles, reason, (int)state_);
    std::printf("  Current PC=$%04X, Opcode=$%02X (%s), Prefix=%d\n",
      regs_.pc, cur_opcode_, cur_op_info_.name ? cur_op_info_.name : "?", cur_prefix_);
    std::printf("  Registers: A=$%02X B=$%02X X=$%04X Y=$%04X S=$%04X U=$%04X DP=$%02X CC=$%02X MD=$%02X (Valid: 0x%04X)\n",
      regs_.a, regs_.b, regs_.x, regs_.y, regs_.s, regs_.u, regs_.dp, regs_.cc, regs_.md, regs_.valid_mask);
#endif
  }

  void HandleResetWait(CycleType type, uint16_t addr, uint8_t data) {
    if (type == CycleType::READ && addr == 0xFFFE) {
      reset_msb_ = data;
      state_ = TrackerState::OPCODE_FETCH;
      is_synced_ = true;
    }
  }

  bool HandleOpcodeFetch(CycleType type, uint16_t addr, uint8_t data) {
    if (type != CycleType::READ) {
      LogMispredict("Expected Opcode Read cycle, got Write");
      EnterResyncHunt(addr);
      return false;
    }

    // Set PC to the opcode address
    regs_.pc = addr;
    cur_instr_start_pc_ = addr;
    stats_.instructions_executed++;

    // Check for Page prefixes
    if (data == 0x10) {
      cur_prefix_ = 2;
      regs_.pc++;
      state_ = TrackerState::PREFIX_PAGE;
      return true; // Prefix fetch is First Instruction Cycle (FIC)!
    } else if (data == 0x11) {
      cur_prefix_ = 3;
      regs_.pc++;
      state_ = TrackerState::PREFIX_PAGE;
      return true; // Prefix fetch is First Instruction Cycle (FIC)!
    }

    // Page 1 Opcode
    cur_prefix_ = 1;
    cur_opcode_ = data;
    cur_op_info_ = opcode_tables_.page1[cur_opcode_];

    if (!cur_op_info_.valid) {
      LogMispredict("Invalid Page 1 Opcode");
      EnterResyncHunt(addr);
      return true;
    }

    regs_.pc++;
    SetupCyclePlan();
    return true; // FIC!
  }

  void HandlePrefixPage(CycleType type, uint16_t addr, uint8_t data) {
    if (type != CycleType::READ || addr != regs_.pc) {
      LogMispredict("Expected Prefix Opcode Read at PC");
      EnterResyncHunt(addr);
      return;
    }

    cur_opcode_ = data;
    if (cur_prefix_ == 2) cur_op_info_ = opcode_tables_.page2[cur_opcode_];
    else cur_op_info_ = opcode_tables_.page3[cur_opcode_];

    if (!cur_op_info_.valid) {
      LogMispredict("Invalid Page 2/3 Opcode");
      EnterResyncHunt(addr);
      return;
    }

    regs_.pc++;
    SetupCyclePlan();
  }

  void SetupCyclePlan() {
    operand_bytes_needed_ = 0;
    operand_bytes_read_ = 0;
    data_bytes_needed_ = cur_op_info_.data_bytes;
    data_bytes_done_ = 0;
    expected_data_write_ = cur_op_info_.is_write;
    indexed_extra_offset_bytes_ = 0;
    indexed_extra_fetches_needed_ = 0;
    indexed_extra_fetches_done_ = 0;
    indexed_indirect_ = false;
    indexed_indirect_ptr_reads_ = 0;
    indexed_indirect_target_ = 0;
    jsr_active_ = false;
    pull_pc_active_ = false;

    switch (cur_op_info_.mode) {
      case AddrMode::INHERENT:
        if (regs_.Is6309Native()) {
          state_ = TrackerState::OPCODE_FETCH;
        } else {
          state_ = TrackerState::EXTRA_FETCH;
        }
        break;

      case AddrMode::IMM8:
      case AddrMode::SETMD:
        operand_bytes_needed_ = 1;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::IMM8_EXTRA:
        operand_bytes_needed_ = 1;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::IMM16:
        operand_bytes_needed_ = 2;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::IMM32:
        operand_bytes_needed_ = 4;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::DIRECT_R:
      case AddrMode::DIRECT_W:
      case AddrMode::DIRECT_RW:
      case AddrMode::JMP_DIR:
      case AddrMode::REL8:
      case AddrMode::BRANCH_ALWAYS_8:
      case AddrMode::BSR:
      case AddrMode::PUSHPULL_S:
      case AddrMode::PUSHPULL_U:
      case AddrMode::EXG_TFR:
      case AddrMode::BIT_DIRECT:
      case AddrMode::INTER_REG:
        operand_bytes_needed_ = 1;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::JSR_DIR:
        operand_bytes_needed_ = 1;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::EXTENDED_R:
      case AddrMode::EXTENDED_W:
      case AddrMode::EXTENDED_RW:
      case AddrMode::JMP_EXT:
      case AddrMode::REL16:
      case AddrMode::BRANCH_ALWAYS_16:
      case AddrMode::LBSR:
      case AddrMode::BIT_EXTENDED:
      case AddrMode::TFM:
        operand_bytes_needed_ = 2;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::JSR_EXT:
        operand_bytes_needed_ = 2;
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::INDEXED_R:
      case AddrMode::INDEXED_W:
      case AddrMode::INDEXED_RW:
      case AddrMode::INDEXED_LEA:
      case AddrMode::JSR_IND:
      case AddrMode::JMP_IND:
      case AddrMode::BIT_INDEXED:
        operand_bytes_needed_ = 1; // Start with postbyte
        state_ = TrackerState::OPERAND_BYTES;
        break;

      case AddrMode::RTS:
        data_bytes_needed_ = 2; // 2 stack pulls
        expected_data_write_ = false;
        if (!regs_.Is6309Native()) {
          state_ = TrackerState::EXTRA_FETCH;
        } else {
          state_ = TrackerState::DATA_ACCESS;
        }
        break;

      case AddrMode::RTI:
        data_bytes_needed_ = 1; // Starts by pulling CC
        expected_data_write_ = false;
        if (!regs_.Is6309Native()) {
          state_ = TrackerState::EXTRA_FETCH;
        } else {
          state_ = TrackerState::DATA_ACCESS;
        }
        break;

      case AddrMode::SWI:
        data_bytes_needed_ = 12; // 12 stack writes
        expected_data_write_ = true;
        swi_vector_reads_ = 0;
        if (!regs_.Is6309Native()) {
          state_ = TrackerState::EXTRA_FETCH;
        } else {
          state_ = TrackerState::DATA_ACCESS;
        }
        break;

      default:
        state_ = TrackerState::OPCODE_FETCH;
        break;
    }
  }

  void HandleOperandBytes(CycleType type, uint16_t addr, uint8_t data) {
    if (type != CycleType::READ || addr != regs_.pc) {
      LogMispredict("Expected Operand Read at PC");
      EnterResyncHunt(addr);
      return;
    }

    if (operand_bytes_read_ < 4) {
      operand_buf_[operand_bytes_read_] = data;
    }
    operand_bytes_read_++;
    regs_.pc++;

    // Indexed mode postbyte analysis
    if (IsIndexedMode(cur_op_info_.mode) && operand_bytes_read_ == 1) {
      indexed_postbyte_ = data;
      DecodeIndexedPostbyte(indexed_postbyte_);
      if (indexed_extra_offset_bytes_ > 0) {
        operand_bytes_needed_ += indexed_extra_offset_bytes_;
        return; // Need more operand bytes at PC
      }
    }

    // Still need more operand bytes?
    if (operand_bytes_read_ < operand_bytes_needed_) {
      return;
    }

    // All operand bytes at PC have been fetched!
    FinishOperandFetching();
  }

  bool IsIndexedMode(AddrMode mode) const {
    return mode == AddrMode::INDEXED_R || mode == AddrMode::INDEXED_W ||
           mode == AddrMode::INDEXED_RW || mode == AddrMode::INDEXED_LEA ||
           mode == AddrMode::JSR_IND || mode == AddrMode::JMP_IND ||
           mode == AddrMode::BIT_INDEXED;
  }

  void DecodeIndexedPostbyte(uint8_t pb) {
    indexed_extra_offset_bytes_ = 0;
    indexed_extra_fetches_needed_ = 0;
    indexed_extra_fetches_done_ = 0;
    indexed_indirect_ = false;

    if ((pb & 0x80) == 0) {
      // 5-bit offset -> 1 extra fetch
      indexed_extra_fetches_needed_ = 1;
      return;
    }

    indexed_indirect_ = (pb & 0x10) != 0;
    uint8_t mod = pb & 0x0F;

    switch (mod) {
      case 0x00: // ,R+
      case 0x01: // ,R++
      case 0x02: // ,-R
      case 0x03: // ,--R
      case 0x04: // ,R (0 offset)
      case 0x05: // B,R
      case 0x06: // A,R
      case 0x07: // E,R
      case 0x0A: // F,R
        indexed_extra_fetches_needed_ = 1;
        break;

      case 0x0B: // D,R
      case 0x0E: // W,R
        indexed_extra_fetches_needed_ = 2;
        break;

      case 0x08: // 8-bit offset
      case 0x0C: // 8-bit PCR
        indexed_extra_offset_bytes_ = 1;
        break;

      case 0x09: // 16-bit offset
      case 0x0D: // 16-bit PCR
        indexed_extra_offset_bytes_ = 2;
        break;

      case 0x0F: // Extended indirect
        indexed_extra_offset_bytes_ = 2;
        indexed_indirect_ = true;
        break;

      default:
        break;
    }
  }

  void FinishOperandFetching() {
    // 6309 SETMD (LDMD #imm)
    if (cur_op_info_.mode == AddrMode::SETMD) {
      regs_.md = operand_buf_[0];
      regs_.SetValid(REG_MD, true);
      state_ = TrackerState::OPCODE_FETCH;
      return;
    }

    // ANDCC / ORCC / CWAI (IMM8_EXTRA)
    if (cur_op_info_.mode == AddrMode::IMM8_EXTRA) {
      SnarfImmediate();
      if (!regs_.Is6309Native()) {
        state_ = TrackerState::EXTRA_FETCH;
      } else {
        state_ = TrackerState::OPCODE_FETCH;
      }
      return;
    }

    // JSR Direct / Extended / Indexed: 1 dummy read at target, then 2 stack writes
    if (cur_op_info_.mode == AddrMode::JSR_DIR || cur_op_info_.mode == AddrMode::JSR_EXT ||
        cur_op_info_.mode == AddrMode::JSR_IND) {
      jsr_active_ = true;
      data_bytes_needed_ = 3; // 1 dummy read + 2 stack writes
      data_bytes_done_ = 0;
      if (cur_op_info_.mode == AddrMode::JSR_EXT) {
        jsr_target_ = (uint16_t(operand_buf_[0]) << 8) | operand_buf_[1];
      }
      if (IsIndexedMode(cur_op_info_.mode) && indexed_extra_fetches_needed_ > 0 && !regs_.Is6309Native()) {
        state_ = TrackerState::EXTRA_FETCH;
      } else {
        state_ = TrackerState::DATA_ACCESS;
      }
      return;
    }

    // JMP Extended
    if (cur_op_info_.mode == AddrMode::JMP_EXT) {
      regs_.pc = (uint16_t(operand_buf_[0]) << 8) | operand_buf_[1];
      state_ = TrackerState::OPCODE_FETCH;
      return;
    }

    // Relative branches
    if (cur_op_info_.mode == AddrMode::REL8 || cur_op_info_.mode == AddrMode::BRANCH_ALWAYS_8) {
      int8_t off = (int8_t)operand_buf_[0];
      branch_target_ = regs_.pc + off;
      branch_fallthrough_ = regs_.pc;

      if (cur_op_info_.mode == AddrMode::BRANCH_ALWAYS_8) {
        regs_.pc = branch_target_;
        state_ = TrackerState::OPCODE_FETCH;
      } else {
        int cond = snarfing_enabled_ ? regs_.EvaluateBranch(cur_opcode_) : -1;
        if (cond == 1) {
          regs_.pc = branch_target_;
          state_ = TrackerState::OPCODE_FETCH;
        } else if (cond == 0) {
          regs_.pc = branch_fallthrough_;
          state_ = TrackerState::OPCODE_FETCH;
        } else {
          state_ = TrackerState::BRANCH_TEST;
        }
      }
      return;
    }

    if (cur_op_info_.mode == AddrMode::REL16 || cur_op_info_.mode == AddrMode::BRANCH_ALWAYS_16) {
      int16_t off = (int16_t)((uint16_t(operand_buf_[0]) << 8) | operand_buf_[1]);
      branch_target_ = regs_.pc + off;
      branch_fallthrough_ = regs_.pc;

      if (cur_op_info_.mode == AddrMode::BRANCH_ALWAYS_16) {
        regs_.pc = branch_target_;
        state_ = TrackerState::OPCODE_FETCH;
      } else {
        int cond = snarfing_enabled_ ? regs_.EvaluateBranch(cur_opcode_) : -1;
        if (cond == 1) {
          regs_.pc = branch_target_;
          state_ = TrackerState::OPCODE_FETCH;
        } else if (cond == 0) {
          regs_.pc = branch_fallthrough_;
          state_ = TrackerState::OPCODE_FETCH;
        } else {
          state_ = TrackerState::BRANCH_TEST;
        }
      }
      return;
    }

    // BSR / LBSR
    if (cur_op_info_.mode == AddrMode::BSR) {
      int8_t off = (int8_t)operand_buf_[0];
      branch_target_ = regs_.pc + off;
      data_bytes_needed_ = 2; // 2 stack writes
      expected_data_write_ = true;
      state_ = TrackerState::DATA_ACCESS;
      return;
    }
    if (cur_op_info_.mode == AddrMode::LBSR) {
      int16_t off = (int16_t)((uint16_t(operand_buf_[0]) << 8) | operand_buf_[1]);
      branch_target_ = regs_.pc + off;
      data_bytes_needed_ = 2;
      expected_data_write_ = true;
      state_ = TrackerState::DATA_ACCESS;
      return;
    }

    // Immediate instructions finish here
    if (cur_op_info_.mode == AddrMode::IMM8 || cur_op_info_.mode == AddrMode::IMM16 ||
        cur_op_info_.mode == AddrMode::IMM32) {
      SnarfImmediate();
      state_ = TrackerState::OPCODE_FETCH;
      return;
    }

    // Push / Pull analysis
    if (cur_op_info_.mode == AddrMode::PUSHPULL_S || cur_op_info_.mode == AddrMode::PUSHPULL_U) {
      uint8_t pb = operand_buf_[0];
      int count = 0;
      bool has_pc = (pb & 0x80) != 0;
      for (int i = 0; i < 8; i++) {
        if ((pb & (1 << i)) != 0) {
          if (i == 4 || i == 5 || i == 6 || i == 7) count += 2; // 16-bit regs
          else count += 1; // 8-bit regs
        }
      }
      data_bytes_needed_ = count;
      data_bytes_done_ = 0;
      expected_data_write_ = cur_op_info_.is_write;
      pull_pc_active_ = (!expected_data_write_ && has_pc);
      pulled_pc_ = 0;

      if (data_bytes_needed_ == 0) {
        state_ = TrackerState::OPCODE_FETCH;
      } else if (expected_data_write_) {
        // PSH: 1 dummy read before writes
        state_ = TrackerState::STACK_DUMMY_READ;
      } else {
        // PUL: direct stack reads
        state_ = TrackerState::DATA_ACCESS;
      }
      return;
    }

    // Indexed mode
    if (IsIndexedMode(cur_op_info_.mode)) {
      if (indexed_extra_fetches_needed_ > 0 && !regs_.Is6309Native()) {
        state_ = TrackerState::EXTRA_FETCH;
      } else if (data_bytes_needed_ > 0 || indexed_indirect_) {
        state_ = TrackerState::DATA_ACCESS;
      } else {
        state_ = TrackerState::OPCODE_FETCH;
      }
      return;
    }

    // Jump / Calls / Direct / Extended
    if (cur_op_info_.has_extra_fetch && !regs_.Is6309Native()) {
      state_ = TrackerState::EXTRA_FETCH;
    } else if (data_bytes_needed_ > 0 || indexed_indirect_) {
      state_ = TrackerState::DATA_ACCESS;
    } else {
      state_ = TrackerState::OPCODE_FETCH;
    }
  }

  void HandleExtraFetch(CycleType type, uint16_t addr, uint8_t data) {
    (void)addr; (void)data;
    if (type == CycleType::READ) {
      if (IsIndexedMode(cur_op_info_.mode)) {
        indexed_extra_fetches_done_++;
        if (indexed_extra_fetches_done_ < indexed_extra_fetches_needed_) {
          return; // Still need more extra fetches (e.g. D,R)
        }
      }
      if (data_bytes_needed_ > 0 || indexed_indirect_ || jsr_active_) {
        state_ = TrackerState::DATA_ACCESS;
      } else {
        state_ = TrackerState::OPCODE_FETCH;
      }
    } else if (type == CycleType::WRITE && expected_data_write_) {
      state_ = TrackerState::DATA_ACCESS;
      HandleDataAccess(type, addr, data);
    } else {
      state_ = TrackerState::DATA_ACCESS;
      HandleDataAccess(type, addr, data);
    }
  }

  void HandleStackDummyRead(CycleType type, uint16_t addr, uint8_t data) {
    (void)type; (void)addr; (void)data;
    if (expected_data_write_) {
      state_ = TrackerState::DATA_ACCESS;
    } else {
      // Finished PULS/PULU stack end dummy read! Next is opcode fetch.
      state_ = TrackerState::OPCODE_FETCH;
    }
  }

  void HandleDataAccess(CycleType type, uint16_t addr, uint8_t data) {
    // Indirect pointer read (2 bytes)
    if (indexed_indirect_ && indexed_indirect_ptr_reads_ < 2) {
      if (indexed_indirect_ptr_reads_ == 0) {
        indexed_indirect_target_ = (uint16_t(data) << 8);
      } else if (indexed_indirect_ptr_reads_ == 1) {
        indexed_indirect_target_ |= data;
      }
      indexed_indirect_ptr_reads_++;
      if (indexed_indirect_ptr_reads_ == 2) {
        if (cur_op_info_.mode == AddrMode::JMP_IND) {
          regs_.pc = indexed_indirect_target_;
          state_ = TrackerState::OPCODE_FETCH;
          return;
        }
        if (jsr_active_) {
          jsr_target_ = indexed_indirect_target_;
          // Now proceed to the 3 JSR data cycles (1 dummy read + 2 stack writes)
          return;
        }
        if (data_bytes_needed_ == 0) {
          state_ = TrackerState::OPCODE_FETCH;
          return;
        }
      }
      return;
    }

    data_bytes_done_++;

    // JSR: 1 dummy read at target, then 2 stack writes
    if (jsr_active_) {
      if (data_bytes_done_ == 1 && type == CycleType::READ) {
        jsr_target_ = addr; // Captured target address from dummy read
      } else if (data_bytes_done_ >= data_bytes_needed_) {
        regs_.pc = jsr_target_;
        state_ = TrackerState::OPCODE_FETCH;
        return;
      }
      return;
    }

    // Pulling registers during PULS/PULU
    if (cur_op_info_.mode == AddrMode::PUSHPULL_S || cur_op_info_.mode == AddrMode::PUSHPULL_U) {
      if (!expected_data_write_) {
        if (pull_pc_active_ && data_bytes_done_ >= data_bytes_needed_ - 1) {
          if (data_bytes_done_ == data_bytes_needed_ - 1) {
            pulled_pc_ = (uint16_t(data) << 8);
          } else if (data_bytes_done_ == data_bytes_needed_) {
            pulled_pc_ |= data;
            regs_.pc = pulled_pc_;
          }
        }
        if (data_bytes_done_ >= data_bytes_needed_) {
          // 1 stack dummy read at end of PULS/PULU
          state_ = TrackerState::STACK_DUMMY_READ;
          return;
        }
      }
    }

    // RTS return PC pulling
    if (cur_op_info_.mode == AddrMode::RTS) {
      if (data_bytes_done_ == 1) {
        pulled_pc_ = (uint16_t(data) << 8);
      } else if (data_bytes_done_ == 2) {
        pulled_pc_ |= data;
        regs_.pc = pulled_pc_;
        state_ = TrackerState::OPCODE_FETCH;
        return;
      }
    }

    // RTI return pulling
    if (cur_op_info_.mode == AddrMode::RTI) {
      if (data_bytes_done_ == 1) {
        regs_.cc = data;
        regs_.SetValid(REG_CC, true);
        rti_entire_ = (data & CC_E) != 0;
        data_bytes_needed_ = rti_entire_ ? 12 : 3;
      } else if (data_bytes_done_ == data_bytes_needed_ - 1) {
        pulled_pc_ = (uint16_t(data) << 8);
      } else if (data_bytes_done_ == data_bytes_needed_) {
        pulled_pc_ |= data;
        regs_.pc = pulled_pc_;
        // 1 stack dummy read before next instruction
        state_ = TrackerState::STACK_DUMMY_READ;
        return;
      }
    }

    // SWI / SWI2 / SWI3
    if (cur_op_info_.mode == AddrMode::SWI) {
      if (data_bytes_done_ == 12) {
        data_bytes_needed_ = 14;
      } else if (data_bytes_done_ == 13) {
        vector_target_ = (uint16_t(data) << 8);
      } else if (data_bytes_done_ == 14) {
        vector_target_ |= data;
        regs_.pc = vector_target_;
        state_ = TrackerState::OPCODE_FETCH;
        return;
      }
    }

    // BSR / LBSR subroutine call
    if (cur_op_info_.mode == AddrMode::BSR || cur_op_info_.mode == AddrMode::LBSR) {
      if (data_bytes_done_ >= data_bytes_needed_) {
        regs_.pc = branch_target_;
        state_ = TrackerState::OPCODE_FETCH;
        return;
      }
    }

    // Standard memory loads/stores
    if (data_bytes_done_ >= data_bytes_needed_) {
      state_ = TrackerState::OPCODE_FETCH;
    }
  }

  bool HandleBranchTest(CycleType type, uint16_t addr, uint8_t data) {
    if (type != CycleType::READ) {
      LogMispredict("Expected Branch Target Read cycle, got Write");
      EnterResyncHunt(addr);
      return false;
    }

    if (addr == branch_target_) {
      // Branch was Taken!
      regs_.pc = addr;
      state_ = TrackerState::OPCODE_FETCH;
      return HandleOpcodeFetch(type, addr, data);
    } else if (addr == branch_fallthrough_) {
      // Branch was Not Taken!
      regs_.pc = addr;
      state_ = TrackerState::OPCODE_FETCH;
      return HandleOpcodeFetch(type, addr, data);
    } else {
      LogMispredict("Branch address matched neither target nor fallthrough");
      EnterResyncHunt(addr);
      return HandleResyncHunt(type, addr, data);
    }
  }

  void EnterResyncHunt(uint16_t addr) {
    stats_.resync_events++;
    state_ = TrackerState::RESYNC_HUNT;
    resync_count_ = 0;
    resync_last_addr_ = addr;
    regs_.valid_mask = 0;
  }

  bool HandleResyncHunt(CycleType type, uint16_t addr, uint8_t data) {
    if (type == CycleType::READ) {
      if (addr == resync_last_addr_ + 1) {
        resync_count_++;
      } else {
        resync_count_ = 1;
      }
      resync_last_addr_ = addr;

      if (resync_count_ >= 2) {
        state_ = TrackerState::OPCODE_FETCH;
        return HandleOpcodeFetch(type, addr, data);
      }
    }
    return false;
  }

  void SnarfImmediate() {
    if (!snarfing_enabled_) return;

    if (cur_op_info_.mode == AddrMode::IMM8 || cur_op_info_.mode == AddrMode::IMM8_EXTRA) {
      uint8_t val = operand_buf_[0];
      switch (cur_opcode_) {
        case 0x86: regs_.a = val; regs_.SetValid(REG_A, true); break; // LDA
        case 0xC6: regs_.b = val; regs_.SetValid(REG_B, true); break; // LDB
        case 0x1A: regs_.cc |= val; regs_.SetValid(REG_CC, true); break; // ORCC
        case 0x1C: regs_.cc &= val; regs_.SetValid(REG_CC, true); break; // ANDCC
        case 0x8F: regs_.e = val; regs_.SetValid(REG_E, true); break; // 6309 LDE
        case 0xCF: regs_.f = val; regs_.SetValid(REG_F, true); break; // 6309 LDF
      }
    } else if (cur_op_info_.mode == AddrMode::IMM16) {
      uint16_t val = (uint16_t(operand_buf_[0]) << 8) | operand_buf_[1];
      if (cur_prefix_ == 1) {
        switch (cur_opcode_) {
          case 0x8E: regs_.x = val; regs_.SetValid(REG_X, true); break; // LDX
          case 0xCC: regs_.SetD(val); break;                             // LDD
          case 0xCE: regs_.u = val; regs_.SetValid(REG_U, true); break; // LDU
          case 0xC7: regs_.SetW(val); break;                             // 6309 LDW
        }
      } else if (cur_prefix_ == 2) {
        switch (cur_opcode_) {
          case 0x8E: regs_.y = val; regs_.SetValid(REG_Y, true); break; // LDY
          case 0xCE: regs_.s = val; regs_.SetValid(REG_S, true); break; // LDS
        }
      }
    }
  }
};

} // namespace cputracker
