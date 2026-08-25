#ifndef V4_INTERRUPTS_H_
#define V4_INTERRUPTS_H_

// v4_interrupts.h — CRTP interrupt management mixins.
//
// The interrupt framework provides a platform-independent API for
// requesting and clearing IRQ/FIRQ/NMI. The actual pin assertion
// is delegated to the platform engine via CRTP:
//
//   T::AssertIRQPin()  / T::ReleaseIRQPin()
//   T::AssertFIRQPin() / T::ReleaseFIRQPin()
//   T::AssertNMIPin()  / T::ReleaseNMIPin()
//
// On TFR911, these directly drive GPIO pins.
// On Centipede, IRQ/FIRQ are typically no-ops (no direct CPU access),
// and NMI uses open-drain GPIO direction switching.

#include "v4_types.h"

// State variables for the interrupt controller.
// These are separate from platform pin state — they track the
// logical "should we be asserting?" state.
inline volatile bool irq_requested   = false;
inline volatile bool firq_requested  = false;
inline volatile bool nmi_requested   = false;

// ── DoInterrupts<T> ──
// Provides interrupt request/clear API. Calls T::Assert/Release*Pin().
template <typename T>
struct DoInterrupts {

  // Called by I/O devices when they want to assert an interrupt.
  FORCE_INLINE static void RequestIRQ() {
    if (!irq_requested) {
      irq_requested = true;
      T::AssertIRQPin();
    }
  }

  FORCE_INLINE static void ClearIRQ() {
    if (irq_requested) {
      irq_requested = false;
      T::ReleaseIRQPin();
    }
  }

  FORCE_INLINE static void RequestFIRQ() {
    if (!firq_requested) {
      firq_requested = true;
      T::AssertFIRQPin();
    }
  }

  FORCE_INLINE static void ClearFIRQ() {
    if (firq_requested) {
      firq_requested = false;
      T::ReleaseFIRQPin();
    }
  }

  FORCE_INLINE static void RequestNMI() {
    if (!nmi_requested) {
      nmi_requested = true;
      T::AssertNMIPin();
    }
  }

  FORCE_INLINE static void ClearNMI() {
    if (nmi_requested) {
      nmi_requested = false;
      T::ReleaseNMIPin();
    }
  }

  // Called from the outer loop to re-evaluate interrupt state.
  // Devices set flags; this function drives the pin accordingly.
  FORCE_INLINE static void PollInterrupts() {
    // Individual devices call RequestIRQ/ClearIRQ directly,
    // so PollInterrupts is a hook for batch evaluation if needed.
  }
};

// ── DontInterrupts<T> ──
// All no-ops. For test builds or platforms without interrupt support.
template <typename T>
struct DontInterrupts {
  FORCE_INLINE static void RequestIRQ() {}
  FORCE_INLINE static void ClearIRQ() {}
  FORCE_INLINE static void RequestFIRQ() {}
  FORCE_INLINE static void ClearFIRQ() {}
  FORCE_INLINE static void RequestNMI() {}
  FORCE_INLINE static void ClearNMI() {}
  FORCE_INLINE static void PollInterrupts() {}
};

#endif  // V4_INTERRUPTS_H_
