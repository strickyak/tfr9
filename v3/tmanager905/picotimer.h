#ifndef _PICOTIMER_H_
#define _PICOTIMER_H_

// If not using PicoTimer, this is the constant
// that determines how often simulated clock interrupts occur.
// VSYNC_TICK_SCALE deterimes a power of two.
constexpr uint VSYNC_TICK_SCALE = 16;
// VSYNC_TICK_MASK will be one less than the power of two.
constexpr uint VSYNC_TICK_MASK = (1u << VSYNC_TICK_SCALE) - 1;

volatile bool TimerFired;

struct repeating_timer TimerData;

bool TimerCallback(repeating_timer_t* rt) {
  TimerFired = true;
  return true;
}

template <class T>
struct DontPicoTimer {
  static constexpr bool DoesPicoTimer() { return false; }
  static void StartTimer(int period_us) {}
};

template <class T>
struct DoPicoTimer {
  static force_inline bool DoesPicoTimer() { return true; }

  static void StartTimer(int period_us) {
    // thanks https://forums.raspberrypi.com/viewtopic.php?t=349809
    MUMBLE("APID");
    alarm_pool_init_default();

    MUMBLE("ART");
    add_repeating_timer_us(16666 /* 60 Hz */, TimerCallback, nullptr,
                           &TimerData);
  }
};

#endif  // _PICOTIMER_H_
