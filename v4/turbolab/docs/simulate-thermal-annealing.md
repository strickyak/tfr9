# Simulated Thermal Annealing & Hill Climbing Tuning Engine

The `turbolab/tool/annealing.go` program is an automated optimization engine designed to minimize the execution runtime of the 6309 CPU on the **TFR/911h co-processor (TurboLab)**. It searches the parameter space of the Hamster PIO clock loop delays ($K_1..K_4$) and timing phase delays ($T_1..T_5$) without requiring firmware recompilation.

---

## 1. System Overview

In the TurboLab architecture, Core 1 of the RP2350 co-processor generates the 6309 dynamic clock signals ($E$ and $Q$) and controls bus timing via PIO state machines. The firmware timing delays are dynamically configurable at startup via the `--tuning` flag:

```bash
./build/tether.linux-amd64.exe -n --tuning=MHZ=250,K1=9,K2=19,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12 data/bootable/pi.decb
```

The optimization engine repeatedly evaluates candidate parameter vectors against a deterministic benchmark program, measures wall-clock runtime, and guides the parameter search using **Simulated Thermal Annealing** or **Greedy Hill Climbing**.

### The Benchmark: `data/bootable/pi.decb`
- Emits 4 consecutive rounds of a 100-digit Rabinowitz-Wagon $\pi$ spigot algorithm.
- Each round produces exactly 102 characters:
  ```text
  3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067
  ```
- Upon completing round 4, the 6309 executes an `swi` instruction. Because non-reset interrupt vectors are unmapped ($0000), the Pico detects a read from vector address `$FFFA` ($0000), prints a Zero Interrupt Vector fault, dumps memory, and exits with status 1.
- **Baseline runtime**: $\approx 49.50\text{ seconds}$ at default parameters (`MHZ=250,K1=9,K2=19,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12`).

---

## 2. Parameter Search Space & Reachable Neighborhood

### Parameter Bounds
- **Fixed Frequency**: `MHZ = 250` (RP2350 system clock frequency).
- **Search Bounds**: Each parameter can vary within $\pm 10$ of its initial value, bounded below by $0$ (nonnegative integers):

| Parameter | Initial ($P_0$) | Min ($P_0 - 10$) | Max ($P_0 + 10$) | Functional Role |
|:---:|:---:|:---:|:---:|:---|
| **K1** | 9  | 0  | 19 | Hamster PIO clock loop delay 1 |
| **K2** | 19 | 9  | 29 | Hamster PIO clock loop delay 2 |
| **K3** | 11 | 1  | 21 | Hamster PIO clock loop delay 3 |
| **K4** | 8  | 0  | 18 | Hamster PIO clock loop delay 4 |
| **T1** | 16 | 6  | 26 | Bus timing phase delay 1 |
| **T2** | 0  | 0  | 10 | Bus timing phase delay 2 |
| **T3** | 22 | 12 | 32 | Bus timing phase delay 3 |
| **T4** | 3  | 0  | 13 | Bus timing phase delay 4 |
| **T5** | 12 | 2  | 22 | Bus timing phase delay 5 |

### Reachable Neighborhood
From any state $P$, candidate neighbor $P'$ is sampled with perturbations $\delta_i \in \{-2, -1, 0, +1, +2\}$ clamped to each parameter's $[ \text{min}_i, \text{max}_i ]$:
- **`single` mode** (default): Randomly selects one parameter $i$ and perturbs it by $\delta_i \in \{-2, -1, +1, +2\}$. This provides smooth, granular hill climbing.
- **`multi` mode**: Independently perturbs all parameters by $\delta_i \in \{-2, -1, 0, +1, +2\}$ simultaneously (ensuring at least one parameter changes).

---

## 3. Fitness Evaluation & Failure Handling

For each candidate $P'$:
1. Tether is launched as a subprocess with flag `-n` (bypassing `stty` and `ReadLine`) and a per-trial timeout (default 75s).
2. The combined stdout/stderr output is scanned for the canonical 102-character line of $\pi$.
3. **Fitness Calculation**:
   - **Success ($\ge 4$ canonical lines)**: $\text{Fitness} = \text{Wall-clock runtime in seconds}$ (e.g. $49.50\text{s}$).
   - **Failure (timeout, crash, bus freeze, or $< 4$ lines)**: $\text{Fitness} = 600.0\text{ seconds}$ (10-minute penalty).
4. **Subprocess Cleanup**: If a trial times out or hangs, `annealing.go` delivers `SIGINT` to allow tether to release `/dev/ttyACM0` cleanly, followed by `SIGKILL` if necessary. An inter-trial settling delay (default 500ms) prevents USB CDC race conditions.

---

## 4. Simulated Annealing Mechanics & Calibration

Let current fitness be $C(P)$ and candidate fitness be $C(P')$, with difference:
$$\Delta C = C(P') - C(P)$$

The decision rule is:
$$\text{Accept}(P') = \begin{cases} 
\text{True} & \text{if } \Delta C < 0 \text{ (runtime improved)} \\
\text{True with probability } p = \exp\left(-\frac{\Delta C}{T}\right) & \text{if } \Delta C \ge 0 \text{ and } T > 0 \\
\text{False} & \text{if } \Delta C \ge 0 \text{ and } T = 0 \text{ (pure hill climbing)}
\end{cases}$$

Temperature decreases geometrically after each trial:
$$T_{k+1} = T_k \times \alpha$$

### Calibrating Temperature against $\Delta C$
- **Small improvements/regressions**: $\Delta C \approx 0.1\text{s}$ to $1.5\text{s}$.
- **Large regressions**: $\Delta C \approx 2\text{s}$ to $5\text{s}$.
- **Crashes/Failures**: $\Delta C \ge 550\text{s}$.

By setting $T_0 \in [2.0, 2.5]$, a $1.0\text{s}$ regression has a $\approx 60\%$ chance of acceptance at the start of the run (allowing escape from local sub-optima), while catastrophic failure states ($\Delta C \ge 550\text{s}$) have an acceptance probability of $\exp(-550 / 2) \approx 10^{-120} \approx 0\%$.

As temperature cools to $T_{\text{final}} \approx 0.01 - 0.02$, the system "freezes", rejecting even a $0.2\text{s}$ regression ($p < 0.005\%$), settling strictly into the local minimum.

---

## 5. Recommended Settings for 100 and 1000 Trials

Using the cooling formula $\alpha = \left(\frac{T_{\text{final}}}{T_0}\right)^{1/N}$ targeting $T_{\text{final}} \approx 0.015$:

| Horizon | Total Runtime | Initial Temp ($T_0$) | Cooling Rate ($\alpha$) | Final Temp ($T_{\text{final}}$) | Strategy |
|:---|:---:|:---:|:---:|:---:|:---|
| **100 Trials** | $\approx 1.4\text{ hours}$ | **`2.0`** | **`0.955`** *(or `0.95`)* | $\approx 0.015$ | Fast daytime exploratory search. Cools into greedy hill climbing around trial 75. |
| **1000 Trials** | $\approx 13.9\text{ hours}$ | **`2.5`** | **`0.995`** | $\approx 0.017$ | Thorough overnight global optimization across multiple basins. |

### Acceptance Probabilities Over the 100-Trial Run ($T_0 = 2.0, \alpha = 0.955$)

| Progress | Temperature | $\Delta C = +0.2\text{s}$ (minor regression) | $\Delta C = +1.0\text{s}$ (moderate regression) | $\Delta C = +3.0\text{s}$ (large regression) |
|:---|:---:|:---:|:---:|:---:|
| **Start (Trial 1)** | $2.00$ | $90.5\%$ | $60.7\%$ | $22.3\%$ |
| **25% (Trial 25)** | $0.63$ | $72.8\%$ | $20.4\%$ | $0.8\%$ |
| **50% (Trial 50)** | $0.20$ | $36.8\%$ | $0.7\%$ | $< 0.0001\%$ |
| **75% (Trial 75)** | $0.06$ | $3.5\%$ | $\approx 0\%$ | $0\%$ |
| **End (Trial 100)** | $0.02$ | $< 0.005\%$ | $0\%$ | $0\%$ |

---

## 6. Build and Usage Instructions

### Compilation
Build both the tether binaries and the annealing tool from the `turbolab` directory:

```bash
cd /home/strick/modoc/coco-shelf/tfr9/v4/turbolab

# Rebuild all tether executables with -n support
make TETHERS

# Build the annealing optimization tool
make ANNEALING
```

Executable output: `build/annealing.exe`.

---

### Command-Line Recipes

#### 1. Baseline Verification & Pure Hill Climbing ($T = 0$)
To test pure hill climbing where only strictly faster parameters are accepted:
```bash
./build/annealing.exe --max-trials=20 --temp=0.0 --log=hillclimb.csv
```

#### 2. 100-Trial Annealing Run (~1.4 Hours)
```bash
./build/annealing.exe --max-trials=100 --temp=2.0 --cooling=0.955 --log=annealing_100.csv
```

#### 3. 1000-Trial Overnight Run (~14 Hours)
```bash
./build/annealing.exe --max-trials=1000 --temp=2.5 --cooling=0.995 --log=annealing_1000.csv
```

#### 4. Resuming from a Known Good Parameter Set
To start the search from a custom baseline:
```bash
./build/annealing.exe \
  --initial="MHZ=250,K1=8,K2=18,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12" \
  --max-trials=100 --temp=1.5 --cooling=0.96 --log=annealing_resume.csv
```

#### 5. Multi-Parameter Perturbations
To perturb multiple timing parameters simultaneously:
```bash
./build/annealing.exe --neighbor=multi --max-trials=100 --temp=2.0 --cooling=0.955
```

---

## 7. Command-Line Options Reference

| Flag | Default | Description |
|:---|:---|:---|
| `--tether` | `./build/tether.linux-amd64.exe` | Path to tether executable |
| `--binary` | `data/bootable/pi.decb` | Benchmark binary path (`.decb`) |
| `--wire` | `/dev/ttyACM0` | USB CDC serial port connected to RP2350 co-processor |
| `--temp` | `0.0` | Initial annealing temperature (`0.0` = pure greedy hill climbing) |
| `--cooling` | `0.95` | Geometric cooling factor $\alpha$ per trial ($T = T \times \alpha$) |
| `--max-trials` | `100` | Maximum number of candidate trials to run (`0` for unlimited) |
| `--timeout` | `1m15s` (`75s`) | Per-trial execution timeout before aborting and killing process |
| `--penalty` | `600.0` | Penalty fitness in seconds assigned to failed or timed-out trials |
| `--neighbor` | `single` | Neighborhood mode: `single` (1 parameter) or `multi` (all parameters) |
| `--initial` | *(defaults)* | Override initial tuning string (e.g. `MHZ=250,K1=9,...`) |
| `--delay` | `500ms` | Settling delay between trials to allow USB CDC buffers to clear |
| `--seed` | `0` | PRNG seed (`0` to seed from current wall-clock timestamp) |
| `--log` | `annealing.csv` | Output path for structured CSV log |

---

## 8. Log Analysis & Applying Results

### CSV Log Structure
The tool logs every trial to `--log` with the following columns:
```csv
trial,timestamp,temperature,mhz,k1,k2,k3,k4,t1,t2,t3,t4,t5,status,runtime_sec,fitness_sec,accepted,best_runtime_sec
0,2026-09-24T17:52:43Z,0.0000,250,9,19,11,8,16,0,22,3,12,OK,49.50,49.50,true,49.50
1,2026-09-24T17:53:33Z,0.0000,250,8,19,11,8,16,0,22,3,12,OK,49.69,49.69,false,49.50
```

### Inspecting Best Results
At any time (or upon `Ctrl+C` interrupt), the tool outputs a summary:
```text
================================================================================
Optimization Summary (Ran 100 trials, 94 successful):
Best Runtime:   47.82 seconds
Best Parameters: K=( 7,18,10, 8) T=(15, 0,21, 3,11)
Tether Flag:    --tuning=MHZ=250,K1=7,K2=18,K3=10,K4=8,T1=15,T2=0,T3=21,T4=3,T5=11
Log saved to:   annealing_100.csv
================================================================================
```

You can directly copy the `Tether Flag` into normal interactive or production tether runs:
```bash
./build/tether.linux-amd64.exe \
  --tuning=MHZ=250,K1=7,K2=18,K3=10,K4=8,T1=15,T2=0,T3=21,T4=3,T5=11 \
  data/bootable/pi.decb
```
