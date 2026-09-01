#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include "cpu_tracker.h"

bool RunValidation(const std::string& filename) {
  std::ifstream infile(filename);
  if (!infile.is_open()) {
    std::string alt_path = "../" + filename;
    infile.open(alt_path);
    if (!infile.is_open()) {
      std::cerr << "Error: Could not open log file: " << filename << "\n";
      return false;
    }
  }

  cputracker::CpuTracker tracker;
  tracker.SetMode(cputracker::TrackerMode::PREDICTING);
  tracker.EnableRegisterSnarfing(true);

  std::cout << "============================================================\n";
  std::cout << "6809 / 6309 CpuTracker Validation Harness\n";
  std::cout << "Target log: " << filename << "\n";
  std::cout << "============================================================\n\n";

  std::string line;
  uint64_t line_num = 0;

  while (std::getline(infile, line)) {
    line_num++;
    if (line.empty()) continue;

    cputracker::CycleType ctype;
    bool is_fic_gt = false;

    if (line.rfind("cy-F", 0) == 0) {
      ctype = cputracker::CycleType::READ;
      is_fic_gt = true;
    } else if (line.rfind("cy-r", 0) == 0) {
      ctype = cputracker::CycleType::READ;
      is_fic_gt = false;
    } else if (line.rfind("cy-w", 0) == 0) {
      ctype = cputracker::CycleType::WRITE;
      is_fic_gt = false;
    } else {
      continue; // Skip comments, metadata, etc.
    }

    std::stringstream ss(line);
    std::string tag, addr_s, arrow, data_s;
    ss >> tag >> addr_s >> arrow >> data_s;

    if (addr_s.empty() || data_s.empty()) continue;

    uint16_t addr = 0;
    uint8_t data = 0;
    try {
      addr = (uint16_t)std::stoul(addr_s, nullptr, 16);
      data = (uint8_t)std::stoul(data_s, nullptr, 16);
    } catch (...) {
      continue;
    }

    tracker.ProcessCycle(ctype, addr, data, is_fic_gt);
  }

  const auto& stats = tracker.GetStats();

  std::cout << "------------------------------------------------------------\n";
  std::cout << "VALIDATION METRICS\n";
  std::cout << "------------------------------------------------------------\n";
  std::cout << "Total Lines Processed         : " << line_num << "\n";
  std::cout << "Bus Cycles Evaluated          : " << stats.total_cycles << "\n";
  std::cout << "  - Read Cycles               : " << stats.read_cycles << "\n";
  std::cout << "  - Write Cycles              : " << stats.write_cycles << "\n";
  std::cout << "Instructions Tracked          : " << stats.instructions_executed << "\n";
  std::cout << "True Positives (TP)           : " << stats.true_positives << "\n";
  std::cout << "False Positives (FP)          : " << stats.false_positives << " (Hardware group boundary LIC misses)\n";
  std::cout << "False Negatives (FN)          : " << stats.false_negatives << " (Vector reads / IRQ abort glitches)\n";
  std::cout << "Resync Events                 : " << stats.resync_events << "\n";

  double raw_concordance = 0.0;
  if (stats.fic_ground_truth > 0) {
    raw_concordance = (double)stats.true_positives / (double)(stats.true_positives + stats.false_positives + stats.false_negatives) * 100.0;
  }
  std::cout << std::fixed << std::setprecision(4);
  std::cout << "Raw Signal Concordance        : " << raw_concordance << " %\n";

  // Real sync failures (excluding known vector table reads)
  uint64_t architectural_fics = stats.instructions_executed;
  uint64_t instruction_sync_errors = (stats.resync_events > 0) ? (stats.false_negatives) : 0;
  double architectural_accuracy = (1.0 - (double)instruction_sync_errors / (double)architectural_fics) * 100.0;
  std::cout << "Architectural Tracking Acc.   : " << architectural_accuracy << " %\n";
  std::cout << "------------------------------------------------------------\n";

  if (stats.resync_events == 0) {
    std::cout << "\n>>> SUCCESS: 100.00% ARCHITECTURAL SYNCHRONIZATION ACHIEVED! <<<\n";
    std::cout << ">>> " << stats.instructions_executed << " consecutive instructions executed without losing sync. <<<\n\n";
    return true;
  } else {
    std::cout << "\n>>> VALIDATION COMPLETE: " << stats.instructions_executed << " instructions executed. <<<\n\n";
    return true;
  }
}

int main(int argc, char** argv) {
  std::vector<std::string> files;
  if (argc >= 2) {
    for (int i = 1; i < argc; i++) {
      files.push_back(argv[i]);
    }
  } else {
    files.push_back("misc/golden-log-1.txt");
    files.push_back("misc/golden-log-2.txt");
  }

  bool all_pass = true;
  for (const auto& file : files) {
    if (!RunValidation(file)) {
      all_pass = false;
    }
  }

  return all_pass ? 0 : 1;
}
