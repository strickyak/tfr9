package main

import (
	"bytes"
	"context"
	"encoding/csv"
	"flag"
	"fmt"
	"math"
	"math/rand"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)

// Canonical 102-character output line of pi produced by pi.decb (1 leading digit, decimal point, 100 decimals).
const CanonicalPi = "3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067"

// ParameterDef defines a single tunable parameter.
type ParameterDef struct {
	Name    string
	Initial int
	Min     int
	Max     int
}

// Default initial parameter definitions
var DefaultParamDefs = []ParameterDef{
	{Name: "K1", Initial: 9, Min: 0, Max: 19},
	{Name: "K2", Initial: 19, Min: 9, Max: 29},
	{Name: "K3", Initial: 11, Min: 1, Max: 21},
	{Name: "K4", Initial: 8, Min: 0, Max: 18},
	{Name: "T1", Initial: 16, Min: 6, Max: 26},
	{Name: "T2", Initial: 0, Min: 0, Max: 10},
	{Name: "T3", Initial: 22, Min: 12, Max: 32},
	{Name: "T4", Initial: 3, Min: 0, Max: 13},
	{Name: "T5", Initial: 12, Min: 2, Max: 22},
}

// Params represents a candidate point in parameter space.
type Params struct {
	Mhz    int
	Values map[string]int
}

// NewInitialParams creates a Params populated with default initial values.
func NewInitialParams(mhz int) Params {
	p := Params{
		Mhz:    mhz,
		Values: make(map[string]int),
	}
	for _, def := range DefaultParamDefs {
		p.Values[def.Name] = def.Initial
	}
	return p
}

// Clone creates a deep copy of Params.
func (p Params) Clone() Params {
	c := Params{
		Mhz:    p.Mhz,
		Values: make(map[string]int, len(p.Values)),
	}
	for k, v := range p.Values {
		c.Values[k] = v
	}
	return c
}

// TuningFlag returns the string formatted for the tether --tuning flag.
// Example: MHZ=250,K1=9,K2=19,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12
func (p Params) TuningFlag() string {
	parts := []string{fmt.Sprintf("MHZ=%d", p.Mhz)}
	for _, def := range DefaultParamDefs {
		parts = append(parts, fmt.Sprintf("%s=%d", def.Name, p.Values[def.Name]))
	}
	return strings.Join(parts, ",")
}

// ShortString returns a compact representation for console logging.
func (p Params) ShortString() string {
	return fmt.Sprintf("K=(%2d,%2d,%2d,%2d) T=(%2d,%2d,%2d,%2d,%2d)",
		p.Values["K1"], p.Values["K2"], p.Values["K3"], p.Values["K4"],
		p.Values["T1"], p.Values["T2"], p.Values["T3"], p.Values["T4"], p.Values["T5"])
}

// GenerateNeighbor samples a neighbor by selecting neighborDimensions parameters to adjust,
// with each adjusted parameter receiving an integer delta in [-distance, +distance].
func GenerateNeighbor(current Params, neighborDimensions int, distance int, rng *rand.Rand) Params {
	if neighborDimensions < 1 {
		neighborDimensions = 1
	}
	if neighborDimensions > len(DefaultParamDefs) {
		neighborDimensions = len(DefaultParamDefs)
	}
	if distance < 1 {
		distance = 1
	}

	var nonZeroDeltas []int
	for d := -distance; d <= distance; d++ {
		if d != 0 {
			nonZeroDeltas = append(nonZeroDeltas, d)
		}
	}
	var allDeltas []int
	for d := -distance; d <= distance; d++ {
		allDeltas = append(allDeltas, d)
	}

	for attempts := 0; attempts < 100; attempts++ {
		candidate := current.Clone()
		perm := rng.Perm(len(DefaultParamDefs))
		chosenIndices := perm[:neighborDimensions]

		// Choose one primary index among the chosen ones that is guaranteed to try a non-zero step
		primaryIdx := chosenIndices[rng.Intn(len(chosenIndices))]

		changed := false
		for _, idx := range chosenIndices {
			def := DefaultParamDefs[idx]
			var delta int
			if idx == primaryIdx {
				delta = nonZeroDeltas[rng.Intn(len(nonZeroDeltas))]
			} else {
				delta = allDeltas[rng.Intn(len(allDeltas))]
			}
			if delta == 0 {
				continue
			}
			newVal := candidate.Values[def.Name] + delta
			if newVal < def.Min {
				newVal = def.Min
			} else if newVal > def.Max {
				newVal = def.Max
			}
			if newVal != candidate.Values[def.Name] {
				candidate.Values[def.Name] = newVal
				changed = true
			}
		}

		if changed {
			return candidate
		}
	}

	// Fallback in case bounds prevented any change across attempts
	candidate := current.Clone()
	for _, idx := range rng.Perm(len(DefaultParamDefs)) {
		def := DefaultParamDefs[idx]
		val := candidate.Values[def.Name]
		if val < def.Max {
			candidate.Values[def.Name] = val + 1
			return candidate
		} else if val > def.Min {
			candidate.Values[def.Name] = val - 1
			return candidate
		}
	}
	return candidate
}

// TrialResult holds the outcome of running a single tether execution.
type TrialResult struct {
	Trial      int
	Timestamp  time.Time
	Params     Params
	Duration   time.Duration
	RuntimeSec float64
	Fitness    float64
	PiLines    int
	Status     string // "OK", "FAIL_LINES", "FAIL_TIMEOUT", "FAIL_ERROR"
	Error      string
}

// RunTrial runs tether with the given parameters and evaluates runtime fitness.
func RunTrial(ctx context.Context, trialNum int, params Params, tetherPath, binaryPath, wire string, timeout time.Duration, penaltySec float64, workDir string) TrialResult {
	result := TrialResult{
		Trial:     trialNum,
		Timestamp: time.Now().UTC(),
		Params:    params,
	}

	trialCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	tuningArg := "--tuning=" + params.TuningFlag()
	wireArg := "--wire=" + wire

	cmd := exec.CommandContext(trialCtx, tetherPath, "-n", wireArg, tuningArg, binaryPath)
	cmd.Dir = workDir

	// On timeout/cancel, send SIGINT first so tether exits cleanly.
	// If still running after 2 seconds, kill it.
	cmd.Cancel = func() error {
		if cmd.Process != nil {
			_ = cmd.Process.Signal(syscall.SIGINT)
		}
		return nil
	}
	cmd.WaitDelay = 2 * time.Second

	var outBuf bytes.Buffer
	cmd.Stdout = &outBuf
	cmd.Stderr = &outBuf

	start := time.Now()
	err := cmd.Run()
	duration := time.Since(start)
	result.Duration = duration
	result.RuntimeSec = duration.Seconds()

	outStr := outBuf.String()
	piCount := strings.Count(outStr, CanonicalPi)
	result.PiLines = piCount

	if trialCtx.Err() == context.DeadlineExceeded {
		result.Status = "FAIL_TIMEOUT"
		result.Fitness = penaltySec
		result.Error = fmt.Sprintf("Timed out after %v", timeout)
		return result
	}

	// Tether exits with code 1 upon CPU fault (Zero Interrupt Vector at $FFFA after SWI).
	// A trial is successful if it produced at least 4 canonical lines of pi.
	if piCount >= 4 {
		result.Status = "OK"
		result.Fitness = result.RuntimeSec
	} else {
		result.Status = "FAIL_LINES"
		result.Fitness = penaltySec
		if err != nil {
			result.Error = fmt.Sprintf("Got %d pi lines (err: %v)", piCount, err)
		} else {
			result.Error = fmt.Sprintf("Got %d pi lines (expected >= 4)", piCount)
		}
	}

	return result
}

// ParseTuningString parses key=val or comma-separated pairs into Params.
func ParseTuningString(str string, base Params) (Params, error) {
	p := base.Clone()
	parts := strings.Split(str, ",")
	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		kv := strings.SplitN(part, "=", 2)
		if len(kv) != 2 {
			continue
		}
		key := strings.ToUpper(strings.TrimSpace(kv[0]))
		val, err := strconv.Atoi(strings.TrimSpace(kv[1]))
		if err != nil {
			return p, fmt.Errorf("invalid number for %s: %w", key, err)
		}
		if key == "MHZ" {
			p.Mhz = val
		} else {
			p.Values[key] = val
		}
	}
	return p, nil
}

func main() {
	var (
		flagTether     = flag.String("tether", "./build/tether.linux-amd64.exe", "path to tether executable")
		flagBinary     = flag.String("binary", "data/bootable/pi.decb", "path to target binary (.decb)")
		flagDir        = flag.String("dir", "", "working directory for tether execution (default auto-detected)")
		flagWire       = flag.String("wire", "/dev/ttyACM0", "serial device connected to Pi Pico")
		flagTemp       = flag.Float64("temp", 0.0, "initial annealing temperature (0.0 = pure hill climbing)")
		flagCooling    = flag.Float64("cooling", 0.95, "cooling rate alpha per trial (T = T * cooling)")
		flagMaxTrials  = flag.Int("max-trials", 100, "maximum number of trials to run (0 for unlimited)")
		flagTimeout    = flag.Duration("timeout", 75*time.Second, "per-trial execution timeout")
		flagPenalty    = flag.Float64("penalty", 600.0, "penalty runtime in seconds for failed trials (default 10 min)")
		flagLog        = flag.String("log", "annealing.csv", "path to CSV log output file")
		flagNeighbor   = flag.Int("neighbor", 1, "number of dimensions to adjust at a time when taking a step (default 1)")
		flagDistance   = flag.Int("distance", 2, "largest integer delta to adjust at a time (default 2, steps in {-2, -1, 0, 1, 2})")
		flagSeed       = flag.Int64("seed", 0, "PRNG seed (0 to use current timestamp)")
		flagInitial    = flag.String("initial", "", "override initial tuning parameters (e.g. MHZ=250,K1=9,...)")
		flagInterDelay = flag.Duration("delay", 500*time.Millisecond, "delay between trials to settle serial port")
	)
	flag.Parse()

	if *flagNeighbor < 1 || *flagNeighbor > len(DefaultParamDefs) {
		fmt.Fprintf(os.Stderr, "Error: -neighbor must be between 1 and %d (got %d)\n", len(DefaultParamDefs), *flagNeighbor)
		os.Exit(1)
	}
	if *flagDistance < 1 {
		fmt.Fprintf(os.Stderr, "Error: -distance must be >= 1 (got %d)\n", *flagDistance)
		os.Exit(1)
	}

	// Setup PRNG seed
	seed := *flagSeed
	if seed == 0 {
		seed = time.Now().UnixNano()
	}
	rng := rand.New(rand.NewSource(seed))

	// Resolve working directory
	workDir := *flagDir
	if workDir == "" {
		// If running from repo root or v4/, check where tether binary lives
		if _, err := os.Stat(*flagTether); err == nil {
			workDir = "."
		} else if _, err := os.Stat(filepath.Join("turbolab", *flagTether)); err == nil {
			workDir = "turbolab"
		} else {
			workDir = "."
		}
	}

	// Prepare initial parameters
	params := NewInitialParams(250)
	if *flagInitial != "" {
		var err error
		params, err = ParseTuningString(*flagInitial, params)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error parsing --initial: %v\n", err)
			os.Exit(1)
		}
	}

	// Open CSV log file
	logFile, err := os.OpenFile(*flagLog, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error opening log file %q: %v\n", *flagLog, err)
		os.Exit(1)
	}
	defer logFile.Close()

	csvWriter := csv.NewWriter(logFile)
	// Write CSV header if file is empty
	fi, err := logFile.Stat()
	if err == nil && fi.Size() == 0 {
		_ = csvWriter.Write([]string{
			"trial", "timestamp", "temperature", "mhz",
			"k1", "k2", "k3", "k4", "t1", "t2", "t3", "t4", "t5",
			"status", "runtime_sec", "fitness_sec", "accepted", "best_runtime_sec",
		})
		csvWriter.Flush()
	}

	// Setup graceful interrupt handling
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 2)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	temp := *flagTemp
	bestParams := params.Clone()
	bestCost := math.Inf(1)
	currentCost := math.Inf(1)

	var totalTrials int
	var successfulTrials int

	printSummary := func() {
		fmt.Printf("\n================================================================================\n")
		fmt.Printf("Optimization Summary (Ran %d trials, %d successful):\n", totalTrials, successfulTrials)
		if !math.IsInf(bestCost, 1) {
			fmt.Printf("Best Runtime:   %.2f seconds\n", bestCost)
			fmt.Printf("Best Parameters: %s\n", bestParams.ShortString())
			fmt.Printf("Tether Flag:    --tuning=%s\n", bestParams.TuningFlag())
		} else {
			fmt.Printf("No successful trials completed.\n")
		}
		fmt.Printf("Log saved to:   %s\n", *flagLog)
		fmt.Printf("================================================================================\n")
	}

	go func() {
		<-sigChan
		fmt.Fprintf(os.Stderr, "\n[Interrupt received. Stopping optimization loop...]\n")
		cancel()
	}()

	fmt.Printf("================================================================================\n")
	fmt.Printf("TurboLab Simulated Annealing / Hill Climbing Tuning Engine\n")
	fmt.Printf("Target:     %s %s\n", *flagTether, *flagBinary)
	fmt.Printf("Initial:    --tuning=%s\n", params.TuningFlag())
	fmt.Printf("Bounds:     +-10 from initial (nonnegative)\n")
	fmt.Printf("Neighbor:   %d dimension(s) per step (max delta +- %d, steps in [-%d..+%d])\n",
		*flagNeighbor, *flagDistance, *flagDistance, *flagDistance)
	fmt.Printf("Initial T:  %.3f (cooling: %.3f)\n", temp, *flagCooling)
	fmt.Printf("Penalty:    %.1fs (timeout: %v)\n", *flagPenalty, *flagTimeout)
	fmt.Printf("Log file:   %s\n", *flagLog)
	fmt.Printf("================================================================================\n\n")

	// ── Trial 0: Baseline Evaluation ─────────────────────────────────────────────
	fmt.Printf("[Trial 000] Baseline Evaluation with initial parameters...\n")
	res0 := RunTrial(ctx, 0, params, *flagTether, *flagBinary, *flagWire, *flagTimeout, *flagPenalty, workDir)
	totalTrials++
	if res0.Status == "OK" {
		successfulTrials++
		currentCost = res0.Fitness
		bestCost = res0.Fitness
		bestParams = params.Clone()
		fmt.Printf("[Trial 000] T=%.3f | %s | Time: %6.2fs (OK, %d pi lines) | BASELINE\n",
			temp, params.ShortString(), res0.RuntimeSec, res0.PiLines)
	} else {
		currentCost = *flagPenalty
		fmt.Printf("[Trial 000] T=%.3f | %s | Time: %6.2fs (%s, %d pi lines: %s) | BASELINE FAILED\n",
			temp, params.ShortString(), res0.Fitness, res0.Status, res0.PiLines, res0.Error)
	}

	_ = csvWriter.Write([]string{
		"0", res0.Timestamp.Format(time.RFC3339), fmt.Sprintf("%.4f", temp),
		strconv.Itoa(params.Mhz),
		strconv.Itoa(params.Values["K1"]), strconv.Itoa(params.Values["K2"]),
		strconv.Itoa(params.Values["K3"]), strconv.Itoa(params.Values["K4"]),
		strconv.Itoa(params.Values["T1"]), strconv.Itoa(params.Values["T2"]),
		strconv.Itoa(params.Values["T3"]), strconv.Itoa(params.Values["T4"]),
		strconv.Itoa(params.Values["T5"]),
		res0.Status, fmt.Sprintf("%.2f", res0.RuntimeSec), fmt.Sprintf("%.2f", res0.Fitness),
		"true", fmt.Sprintf("%.2f", bestCost),
	})
	csvWriter.Flush()

	time.Sleep(*flagInterDelay)

	// ── Optimization Loop ────────────────────────────────────────────────────────
	trial := 1
	for {
		if ctx.Err() != nil {
			break
		}
		if *flagMaxTrials > 0 && trial > *flagMaxTrials {
			break
		}

		candidate := GenerateNeighbor(params, *flagNeighbor, *flagDistance, rng)
		res := RunTrial(ctx, trial, candidate, *flagTether, *flagBinary, *flagWire, *flagTimeout, *flagPenalty, workDir)
		if ctx.Err() != nil {
			break
		}
		totalTrials++

		deltaC := res.Fitness - currentCost
		accepted := false
		decisionNote := "REJECTED"

		if res.Status == "OK" {
			successfulTrials++
			if deltaC < 0 {
				// Improvement
				accepted = true
				if res.Fitness < bestCost {
					decisionNote = "ACCEPTED (NEW BEST!)"
				} else {
					decisionNote = "ACCEPTED (Improved)"
				}
			} else if temp > 0 && res.Fitness < *flagPenalty {
				// Metropolis criterion for annealing
				prob := math.Exp(-deltaC / temp)
				if rng.Float64() < prob {
					accepted = true
					decisionNote = fmt.Sprintf("ACCEPTED (Annealing p=%.3f)", prob)
				}
			}
		} else {
			decisionNote = "REJECTED (Failed)"
		}

		if accepted {
			params = candidate.Clone()
			currentCost = res.Fitness
			if currentCost < bestCost {
				bestCost = currentCost
				bestParams = candidate.Clone()
			}
		}

		// Print trial outcome
		fmt.Printf("[Trial %03d] T=%.3f | %s | Time: %6.2fs (%s) | %s | Best: %6.2fs\n",
			trial, temp, candidate.ShortString(), res.Fitness, res.Status, decisionNote, bestCost)

		// Log to CSV
		_ = csvWriter.Write([]string{
			strconv.Itoa(trial), res.Timestamp.Format(time.RFC3339), fmt.Sprintf("%.4f", temp),
			strconv.Itoa(candidate.Mhz),
			strconv.Itoa(candidate.Values["K1"]), strconv.Itoa(candidate.Values["K2"]),
			strconv.Itoa(candidate.Values["K3"]), strconv.Itoa(candidate.Values["K4"]),
			strconv.Itoa(candidate.Values["T1"]), strconv.Itoa(candidate.Values["T2"]),
			strconv.Itoa(candidate.Values["T3"]), strconv.Itoa(candidate.Values["T4"]),
			strconv.Itoa(candidate.Values["T5"]),
			res.Status, fmt.Sprintf("%.2f", res.RuntimeSec), fmt.Sprintf("%.2f", res.Fitness),
			strconv.FormatBool(accepted), fmt.Sprintf("%.2f", bestCost),
		})
		csvWriter.Flush()

		// Cool temperature if active
		if temp > 0 {
			temp = temp * (*flagCooling)
		}

		trial++
		time.Sleep(*flagInterDelay)
	}

	printSummary()
}
