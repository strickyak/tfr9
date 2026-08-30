#ifndef _BENCHMARK_CYCLES_H_
#define _BENCHMARK_CYCLES_H_

//////////////////////////////////////////////////////////////
//
// This will print ^{ and then ^} as ordinary terminal output,
// on a cycle of 0x400000 steps.  The begin and end marks
// will be 0x200000 cycles apart.  These are the marks that
// begin and end a benchmarking timer in tconsole.h
// (which also prints the average of all such timers).
//
// Result with 270MHz div3 machine X1_Fast (coco2-like RAM):
// 0x200000 cycles / 0.811053 average seconds =
// 2.585715 MHz, on average
//
// Result with 270MHz div3 machine X2_Fast (coco3-like RAM):
// 0x200000 cycles / 0.831228 average seconds =
// 2.522956 MHz, on average
//
// Result with 270MHz div3 machine L1_Fast (coco2-like RAM):
// 0x200000 cycles / 0.811768 average seconds =
// 2.583438 MHz, on average
//
// Result with 270MHz div3 machine L2_Fast (coco3-like RAM):
// 0x200000 cycles / 0.850014 average seconds =
// 2.467197 MHz, on average

template <typename T>
struct DontBenchmarkCycles {
  static force_inline void BenchmarkCycle(uint cy) {}
};

template <typename T>
struct DoBenchmarkCycles {
  static force_inline void BenchmarkCycle(uint cy) {
    uint c = 0x3FFFFF & cy;
    if (c == 0x100000) {
      ShowStr("^{");  // start timer in tconsole
    } else if (c == 0x300000) {
      ShowStr("^}");  // end timer in tconsole
    }
  }
};

#endif  // _BENCHMARK_CYCLES_H_
