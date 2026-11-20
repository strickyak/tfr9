#pragma once
// background.h -- periodic_status and the background loop (core 0 scheduler).
// Moved VERBATIM out of products/tfr911h/main.cpp (v5 Phase 3 step 1: pure code motion, no change
// in behavior; the object code was compared before and after).  Included once, by main.cpp, at the
// point where this code used to be, so it relies on the declarations above that include.

// ═══════════════════════════════════════════════════════════════════
// periodic_status — print diagnostics every 5 seconds (inline)
// ═══════════════════════════════════════════════════════════════════
static uint64_t last_status_time = 0;
static uint32_t last_chars_drained = 0;
static uint32_t last_events_drained = 0;
#if SPEED_STATS
static uint64_t last_idle = 0;
static uint64_t last_active = 0;
#endif

inline void periodic_status() {
  uint64_t now = time_us_64();
  if (now - last_status_time < 5000000) return;
  last_status_time = now;

  uint32_t chars_delta = bg_chars_drained - last_chars_drained;
  uint32_t events_delta = bg_events_drained - last_events_drained;
  last_chars_drained = bg_chars_drained;
  last_events_drained = bg_events_drained;

#if SPEED_STATS
  uint64_t idle_delta = fg_idle_cycles - last_idle;
  uint64_t active_delta = fg_active_cycles - last_active;
  last_idle = fg_idle_cycles;
  last_active = fg_active_cycles;
  cobs_printf("\n[bg: chars=%d/%d events=%d/%d ch_d=%u ev_d=%u idle=%uM active=%uM halt=%d bg_halt=%d]\n",
              (int)fg2bg_chars.size(), 8192,
              (int)fg2bg.size(), 8192,
              chars_delta, events_delta,
              (unsigned)(idle_delta / 1000000),
              (unsigned)(active_delta / 1000000),
              666, //== (int)fg_wants_halt,
              (int)bg_wants_halt);
#else
  if (0) cobs_printf("\n[bg: chars=%d/%d events=%d/%d ch_d=%u ev_d=%u halt=%d bg_halt=%d]\n",
              (int)fg2bg_chars.size(), 8192,
              (int)fg2bg.size(), 8192,
              chars_delta, events_delta,
              666, //== (int)fg_wants_halt,
              (int)bg_wants_halt);
#endif
}

// ═══════════════════════════════════════════════════════════════════
// Background loop (core 0) — Unified Cooperative Scheduler
//
// Phase 1: Tcl REPL (6309 idle, core 1 not launched)
// Phase 2: Bus operation (6309 running, coroutine-based drain)
//
// The scheduler pumps USB between every coroutine resume, exactly
// like the Centipede.  drain_chars() runs inline at the top of
// every iteration so console output is never delayed.
// ═══════════════════════════════════════════════════════════════════
void IN_RAM tfr911_background() {
  // ── Initialize ──────────────────────────────────────────────────
  init_lfs();
  global_tcl_interp = Tcl_CreateInterp();
  register_tcl_commands(global_tcl_interp);
  register_ports_commands(global_tcl_interp);   // after the library: it replaces `bye`
  RegisterEnhancementTclCommands(global_tcl_interp);

  // Create all coroutines upfront
  Coro tcl_repl_coro;
  coro_create(&tcl_repl_coro, tcl_repl_task, tcl_stack, sizeof(tcl_stack));

  Coro drain_coro;
  coro_create(&drain_coro, drain_task, drain_stack, sizeof(drain_stack));

  bool phase2 = false;
  uint64_t last_tick_20ms = 0;

  // ── Unified Scheduler Loop ─────────────────────────────────────
  while (true) {
    // HIGH PRIORITY: drain console characters first (inline, fast)
    drain_chars();
    rtc_poll_walltime();

    if (!phase2) {
      // ── Phase 1: Tcl REPL ──────────────────────────────────────
      coro_resume(&tcl_repl_coro);
      PumpUsbCobs();

      if (tcl_repl_done) {
        // ── Transition to Phase 2 ────────────────────────────────
        //
        // The 6309 is CMOS but not fully static — it needs continuous
        // E/Q clocks or it loses state.  During the Tcl REPL, no
        // clocks were generated.  So we must do the reset WITH the
        // PIO/foreground running:
        //
        //   1. Assert RESET + HALT (open-drain) from background
        //   2. Re-init PIO and launch foreground on core 1
        //   3. Foreground generates E/Q; 6309 sees IDLE cycles
        //   4. Background waits for foreground to be running
        //   5. Hold RESET for ~10ms (~28K E cycles)
        //   6. Release RESET (still HALTed)
        //   7. Wait ~5ms for 6309 to see RESET de-assert
        //   8. Release HALT → 6309 fetches reset vector
        //
        cobs_printf("[bye: asserting RESET+HALT...]\n");
        gpio_set_dir(RESET, GPIO_OUT);
        HaltOn();

        pio_sm_set_enabled(pio0, 0, false);
        pio_sm_restart(pio0, 0);
        pio_clear_instruction_memory(pio0);
        uint offset = pio_add_program(pio0, &hamster_program);
        hamster_program_init(pio0, 0, offset);

        // Resolve the active (on, run, for) target approved by `bye` and run its core-0 setup.
        turbolab_rt::g_active = false;
        const TargetSpec* active_tgt = nullptr;
        ResolveActiveTarget(g_port_settings, &active_tgt, kBoardHost);
        const TargetLauncher* launcher = active_tgt ? FindLauncher(active_tgt->run_os, active_tgt->for_hw) : nullptr;
        if (!launcher || !launcher->setup) {
          centipede_abort("bye: missing target launcher");
        }
        launcher->setup();

        ForegroundFn fg = FindHamsterEngine(active_tgt->engine);
        if (!fg) {
          centipede_abort("bye: unknown hamster engine");
        }

        foreground_running.store(false, std::memory_order_relaxed);
        hamster_cpu_released.store(false, std::memory_order_relaxed);
        cobs_printf("[bye: launching core1 (%s)...]\n", active_tgt->engine);
        multicore_launch_core1(fg);

        while (!foreground_running.load(std::memory_order_acquire)) {
          sleep_ms(1);
        }

        cobs_printf("[bye: foreground running, holding RESET...]\n");
        sleep_ms(10);
        gpio_set_dir(RESET, GPIO_IN);
        cobs_printf("[bye: RESET released, holding HALT...]\n");
        sleep_ms(5);
        hamster_cpu_released.store(true, std::memory_order_release);
        if (turbolab_rt::g_active) {
          turbolab_rt::g_cpu_started.store(true, std::memory_order_release);
        }
        sleep_ms(1);
        HaltOff();
        cobs_printf("[bye: HALT released, 6309 booting]\n");

        alarm_pool_init_default();
        start_20ms_timer();
#if SPEED_STATS
        add_repeating_timer_us(1000, TimerCallback, nullptr, &TimerData);
#endif
#if CLOCK_IRQ
#if TRACE
        add_repeating_timer_ms(5000, Timer60HzCallback, nullptr, &Timer60HzData);
#else
        add_repeating_timer_us(16667, Timer60HzCallback, nullptr, &Timer60HzData);
#endif
#endif
        last_status_time = time_us_64();
        g_guest_boot_us = last_status_time;
        cobs_printf("[bye: Phase 2 started]\n");
        phase2 = true;
      }
    } else {
      // ── Phase 2: 6309 running ──────────────────────────────────
      coro_resume(&drain_coro);
      PumpUsbCobs();
      Phase2PollEnhancements();
      turbolab_poll_core0();

      // Deliver terminal input to the active engine's RX hook
      PollTermInput();
      if (g_rx_poll_fn) {
        g_rx_poll_fn();
      }

      uint64_t cur_tick = get_system_ticks_20ms();
      if (cur_tick != last_tick_20ms) {
        last_tick_20ms = cur_tick;
        keyboard_injector::tick();
      }

      periodic_status();
    }
  }
}


