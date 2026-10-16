package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"math"
	"math/big"
	"os"
	"path/filepath"
	"strconv"

	"github.com/tuneinsight/lattigo/v6/utils/bignum"
)

const width = 6

type manifest struct {
	Schema             int         `json:"schema"`
	Provenance         string      `json:"provenance"`
	AuthorCoefficients bool        `json:"author_coefficients"`
	Width              int         `json:"width"`
	Degree             int         `json:"degree"`
	IntegerDomain      [2]int      `json:"integer_domain"`
	ArgumentMap        argumentMap `json:"argument_map"`
	ArgumentOffset     string      `json:"argument_offset"`
	SignedConvention   string      `json:"signed_convention"`
	InputError         string      `json:"input_error"`
	CoefficientBasis   string      `json:"coefficient_basis"`
	PrecisionBits      uint        `json:"precision_bits"`
	ScanStep           string      `json:"scan_step"`
	MaxError           []string    `json:"remez_max_error"`
	Coefficients       [][]string  `json:"coefficients"`
	ContinuumBound     bool        `json:"continuum_bound_proved"`
}

type argumentMap struct {
	CenterNumerator string `json:"center_numerator"`
	CenterDivisor   int    `json:"center_divisor"`
	ErrorScale      string `json:"error_scale"`
	EncodedValue    string `json:"encoded_affine_value"`
	EncodedScale    int    `json:"encoded_affine_scale"`
	EncodedInteger  int    `json:"encoded_affine_integer"`
}

func exactFloat(numerator, denominator int64, precision uint) *big.Float {
	rational := new(big.Rat).SetFrac(big.NewInt(numerator), big.NewInt(denominator))
	return new(big.Float).SetPrec(precision).SetRat(rational)
}

func chebyshevXMultiply(input []*big.Float, precision uint) []*big.Float {
	output := make([]*big.Float, len(input)+1)
	for i := range output {
		output[i] = new(big.Float).SetPrec(precision)
	}
	for degree, coefficient := range input {
		if degree == 0 {
			output[1].Add(output[1], coefficient)
			continue
		}
		half := new(big.Float).SetPrec(precision).Quo(
			coefficient, new(big.Float).SetPrec(precision).SetInt64(2))
		output[degree-1].Add(output[degree-1], half)
		output[degree+1].Add(output[degree+1], half)
	}
	return output
}

func convertToRawChebyshev(coefficients []*big.Float, outerRadius *big.Float, precision uint) []*big.Float {
	// Lattigo fits in x/outerRadius. Expand T_n(x/outerRadius) back into T_k(x)
	// before exporting coefficients for Cheddar's untransformed input.
	alpha := new(big.Float).SetPrec(precision).Quo(
		new(big.Float).SetPrec(precision).SetInt64(1), outerRadius)
	twoAlpha := new(big.Float).SetPrec(precision).Mul(
		alpha, new(big.Float).SetPrec(precision).SetInt64(2))

	t := []*big.Float{new(big.Float).SetPrec(precision).SetInt64(1)}
	var tPrev []*big.Float
	result := make([]*big.Float, len(coefficients))
	for i := range result {
		result[i] = new(big.Float).SetPrec(precision)
	}

	for degree, coefficient := range coefficients {
		for index := range t {
			result[index].Add(result[index],
				new(big.Float).SetPrec(precision).Mul(coefficient, t[index]))
		}
		if degree+1 == len(coefficients) {
			break
		}
		var next []*big.Float
		if degree == 0 {
			next = []*big.Float{
				new(big.Float).SetPrec(precision),
				new(big.Float).SetPrec(precision).Set(alpha),
			}
		} else {
			xTimesT := chebyshevXMultiply(t, precision)
			next = make([]*big.Float, len(xTimesT))
			for index := range next {
				next[index] = new(big.Float).SetPrec(precision).Mul(twoAlpha, xTimesT[index])
				if index < len(tPrev) {
					next[index].Sub(next[index], tPrev[index])
				}
			}
		}
		tPrev, t = t, next
	}
	return result
}

func main() {
	degree := flag.Int("degree", 47, "odd Chebyshev polynomial degree")
	maxInteger := flag.Int("max-abs-integer", 21, "maximum absolute runtime integer")
	output := flag.String("output", "build/research/discrete_direct6_remez.json", "output manifest")
	precision := flag.Uint("precision", 256, "Remez big.Float precision")
	maxIterations := flag.Int("max-iterations", 200, "maximum Remez iterations")
	threshold := flag.Float64("threshold", 1e-12, "relative Remez convergence threshold")
	bitLimit := flag.Int("bits", width, "number of low-order bits to fit")
	scanDenominator := flag.Int64("scan-denominator", 64, "scan step is cell radius divided by this value")
	flag.Parse()

	if *degree < 1 || *degree%2 == 0 || *maxInteger < 1 || *maxInteger >= 32 {
		panic("degree must be positive odd and max-abs-integer must be in [1,31]")
	}
	if *precision < 128 || *bitLimit < 1 || *bitLimit > width || *scanDenominator < 1 {
		panic("Remez precision must be at least 128 bits")
	}

	lo, hi := -*maxInteger-1, *maxInteger
	divisor := int64(2*hi + 1)
	domainScale := float64(divisor) / 2
	radius := exactFloat(1, 1<<12, *precision)
	radius.Quo(radius, exactFloat(divisor, 2, *precision))
	intervals := make([]bignum.Interval, 0, hi-lo+1)
	for integer := lo; integer <= hi; integer++ {
		center := exactFloat(int64(2*integer+1), divisor, *precision)
		a := new(big.Float).SetPrec(*precision).Sub(center, radius)
		b := new(big.Float).SetPrec(*precision).Add(center, radius)
		intervals = append(intervals, bignum.Interval{A: *a, B: *b, Nodes: 1})
	}

	extraNodes := *degree + 2 - len(intervals)
	if extraNodes < 0 {
		panic("degree is too small for the Lattigo multi-interval initializer")
	}
	for i := 0; i < extraNodes; i++ {
		index := (i*len(intervals) + extraNodes/2) / extraNodes
		if index >= len(intervals) {
			index = len(intervals) - 1
		}
		intervals[index].Nodes++
	}

	scanStep := new(big.Float).SetPrec(*precision).Quo(
		radius, new(big.Float).SetPrec(*precision).SetInt64(*scanDenominator))
	result := manifest{
		Schema:             1,
		Provenance:         fmt.Sprintf("Lattigo v6 utils/bignum.Remez, multi-interval Chebyshev basis; runtime cells [%d,%d] with symmetric certificate extension [%d,%d]", -hi, hi, lo, hi),
		AuthorCoefficients: false,
		Width:              width,
		Degree:             *degree,
		IntegerDomain:      [2]int{lo, hi},
		ArgumentMap: argumentMap{
			CenterNumerator: "(2*I+1)",
			CenterDivisor:   int(divisor),
			ErrorScale:      fmt.Sprintf("2/%d", divisor),
			EncodedValue:    fmt.Sprintf("2/%d", divisor),
			EncodedScale:    int(divisor) * 24,
			EncodedInteger:  48,
		},
		ArgumentOffset:   "0.5",
		SignedConvention: "offset binary: unsigned=I+2^(width-1)",
		InputError:       "0.000244140625",
		CoefficientBasis: "Chebyshev",
		PrecisionBits:    *precision,
		ScanStep:         scanStep.Text('g', 12),
		MaxError:         make([]string, *bitLimit),
		Coefficients:     make([][]string, *bitLimit),
		ContinuumBound:   false,
	}

	outerRadius := new(big.Float).SetPrec(*precision).Add(
		new(big.Float).SetPrec(*precision).SetInt64(1), radius)
	for bit := 0; bit < *bitLimit; bit++ {
		targetBit := bit
		remainder := func(x *big.Float) *big.Float {
			x64, _ := x.Float64()
			integer := int(math.Round(x64*domainScale - 0.5))
			value := ((integer + 32) >> targetBit) & 1
			return exactFloat(int64(2*value-1), 2, *precision)
		}
		r := bignum.NewRemez(bignum.RemezParameters{
			Function:        remainder,
			Basis:           bignum.Chebyshev,
			Intervals:       append([]bignum.Interval(nil), intervals...),
			ScanStep:        new(big.Float).SetPrec(*precision).Set(scanStep),
			Prec:            *precision,
			OptimalScanStep: false,
		})
		r.Approximate(*maxIterations, *threshold)
		if r.Degree != *degree {
			panic(fmt.Sprintf("Lattigo Remez degree mismatch: got %d", r.Degree))
		}
		result.MaxError[bit] = r.MaxErr.Text('g', 40)

		raw := convertToRawChebyshev(r.Coeffs, outerRadius, *precision)
		row := make([]string, *degree+1)
		for i := range row {
			if i == 0 {
				row[i] = "0.5"
			} else if i%2 == 0 {
				row[i] = "0"
			} else {
				row[i] = strconv.FormatFloat(mustFloat64(raw[i]), 'g', 17, 64)
			}
		}
		result.Coefficients[bit] = row
		fmt.Printf("bit=%d degree=%d Remez_max_error=%s\n",
			bit, *degree, result.MaxError[bit])
	}

	encoded, err := json.MarshalIndent(result, "", "  ")
	if err != nil {
		panic(err)
	}
	if err = os.MkdirAll(filepath.Dir(*output), 0o755); err != nil {
		panic(err)
	}
	if err = os.WriteFile(*output, append(encoded, '\n'), 0o644); err != nil {
		panic(err)
	}
	fmt.Println("manifest:", *output)
}

func mustFloat64(value *big.Float) float64 {
	result, _ := value.Float64()
	if math.IsNaN(result) || math.IsInf(result, 0) {
		panic("non-finite coefficient during binary64 export")
	}
	return result
}
