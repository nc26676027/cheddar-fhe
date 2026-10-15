package main

import (
	"encoding/json"
	"math"
	"math/big"
	"math/cmplx"
	"os"
	"sort"
	"strconv"

	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"github.com/tuneinsight/lattigo/v6/schemes/ckks"
	"paper1-meboot/identity"
	"paper1-meboot/identity/presets"
)

// This plaintext-only oracle measures matrix quantization without encryption,
// evaluation keys, ciphertext rescaling or the nonlinear correction.
func main() {
	core, err := presets.Identity16Selected(identity.RouteJoint4434)
	if err != nil { panic(err) }
	params := core.Parameters.BootstrappingParameters
	exponent := 0.0
	if len(os.Args) > 1 {
		exponent, err = strconv.ParseFloat(os.Args[1], 64)
		if err != nil || math.IsNaN(exponent) || exponent < -4 || exponent > 4 {
			panic("gain exponent out of range")
		}
	}
	middleLog := 37.49017137195595
	if len(os.Args) > 2 {
		middleLog, err = strconv.ParseFloat(os.Args[2], 64)
		if err != nil || math.IsNaN(middleLog) || middleLog < 34 || middleLog > 41 {
			panic("intermediate scale out of range")
		}
	}
	outputLog := 32.0
	if len(os.Args) > 3 {
		outputLog, err = strconv.ParseFloat(os.Args[3], 64)
		if err != nil || math.IsNaN(outputLog) || outputLog < 30 || outputLog > 35 {
			panic("output scale out of range")
		}
	}
	encoder := ckks.NewEncoder(params)
	n := params.MaxSlots()
	inputs := make([][]complex128, 4)
	names := []string{"signed-grid", "zeros", "endpoints", "independent"}
	for pattern := range inputs {
		inputs[pattern] = make([]complex128, n)
		for i := range inputs[pattern] {
			x := -.25 + .5 * float64(i) / float64(n-1)
			y := -.25 + .5 * float64((i*1031)%n) / float64(n-1)
			switch names[pattern] {
			case "signed-grid": y = -x
			case "zeros": x, y = 0, 0
			case "endpoints":
				x, y = .25, -.25
				if i%2 != 0 { x = -.25 }
				if i%3 != 0 { y = .25 }
			case "independent":
				if i%3 == 0 { x = 0 }
				if i%5 == 0 { y = 0 }
			}
			inputs[pattern][i] = complex(x, y)
		}
	}
	ideal := make([][]complex128, len(inputs))
	quantized := make([][]complex128, len(inputs))
	for i := range inputs {
		ideal[i] = append([]complex128(nil), inputs[i]...)
		quantized[i] = append([]complex128(nil), inputs[i]...)
	}
	stc := core.Parameters.SlotsToCoeffsParameters
	stc.Scaling = new(big.Float).SetFloat64(1.0 / 64)
	middle := math.Exp2(middleLog)
	scales := []float64{float64(params.Q()[2])*middle/math.Exp2(40),
		float64(params.Q()[1])*math.Exp2(outputLog)/middle}
	for layer := 0; layer < stc.Depth(false); layer++ {
		matrix := stc.GenMatricesSlice(params.LogN(), params.EncodingPrecision(), layer, 1)[0]
		gain := math.Exp2(float64(exponent))
		if layer == 1 { gain = 1 / gain }
		multiplier := new(big.Float).SetFloat64(gain)
		for _, diagonal := range matrix {
			for _, coefficient := range diagonal {
				coefficient[0].Mul(coefficient[0], multiplier)
				coefficient[1].Mul(coefficient[1], multiplier)
			}
		}
		nextIdeal := make([][]complex128, len(inputs))
		nextQuantized := make([][]complex128, len(inputs))
		for pattern := range inputs {
			nextIdeal[pattern] = make([]complex128, n)
			nextQuantized[pattern] = make([]complex128, n)
		}
		indices := matrix.DiagonalsIndexList()
		sort.Ints(indices)
		for _, rotation := range indices {
			diagonal := matrix[rotation]
			pt := ckks.NewPlaintext(params, 2-layer)
			pt.Scale = rlwe.NewScale(scales[layer])
			if err := encoder.Encode(diagonal, pt); err != nil { panic(err) }
			decoded := make([]complex128, n)
			if err := encoder.Decode(pt, decoded); err != nil { panic(err) }
			for j := 0; j < n; j++ {
				source := (j + rotation + n) % n
				exact := diagonal[j].Complex128()
				for pattern := range inputs {
					nextIdeal[pattern][j] += exact * ideal[pattern][source]
					nextQuantized[pattern][j] += decoded[j] * quantized[pattern][source]
				}
			}
		}
		ideal, quantized = nextIdeal, nextQuantized
	}
	cts := core.Parameters.CoeffsToSlotsParameters
	// RepackImagAsReal's unsplit CtS has the half gain before channel split.
	cts.Scaling = new(big.Float).SetFloat64(128)
	for layer := 0; layer < cts.Depth(false); layer++ {
		matrix := cts.GenMatricesSlice(params.LogN(), params.EncodingPrecision(), layer, 1)[0]
		nextIdeal := make([][]complex128, len(inputs))
		nextQuantized := make([][]complex128, len(inputs))
		for pattern := range inputs {
			nextIdeal[pattern] = make([]complex128, n)
			nextQuantized[pattern] = make([]complex128, n)
		}
		indices := matrix.DiagonalsIndexList()
		sort.Ints(indices)
		for _, rotation := range indices {
			for j, coefficient := range matrix[rotation] {
				source := (j + rotation + n) % n
				for pattern := range inputs {
					nextIdeal[pattern][j] += coefficient.Complex128() * ideal[pattern][source]
					nextQuantized[pattern][j] += coefficient.Complex128() * quantized[pattern][source]
				}
			}
		}
		ideal, quantized = nextIdeal, nextQuantized
	}
	results := []any{}
	for pattern := range inputs {
		exactError, quantizationError := 0.0, 0.0
		for i := range inputs[pattern] {
			exactError = math.Max(exactError, cmplx.Abs(ideal[pattern][i]-inputs[pattern][i]))
			quantizationError = math.Max(quantizationError, cmplx.Abs(quantized[pattern][i]-inputs[pattern][i]))
		}
		if exactError > 1e-12 { panic("unencoded inverse oracle mismatch") }
		results = append(results, map[string]any{"pattern": names[pattern],
			"ideal_error": exactError, "encoding_only_error": quantizationError,
			"pass_2m18": quantizationError <= math.Exp2(-18)})
	}
	if err := json.NewEncoder(os.Stdout).Encode(map[string]any{
		"reference_revision": "6330f64b178b6215edd35baf5b11c8da558ca95c",
		"diagnostic_gain_rebalance_log2": exponent,
		"diagnostic_stc_intermediate_log_scale": middleLog,
		"diagnostic_stc_output_log_scale": outputLog,
		"factor_gains": []float64{math.Exp2(float64(exponent))/8, math.Exp2(float64(-exponent))/8},
		"plaintext_scales": scales, "patterns": results,
	}); err != nil { panic(err) }
}
