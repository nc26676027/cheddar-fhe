package main

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"math/cmplx"
	"os"
	"time"

	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"github.com/tuneinsight/lattigo/v6/schemes/ckks"
	"paper1-meboot/identity"
	"paper1-meboot/identity/presets"
)

func main() {
	route := identity.RouteOrdinary
	if len(os.Args) > 1 && os.Args[1] == "joint4434" {
		route = identity.RouteJoint4434
	}
	core, err := presets.Identity16Selected(route)
	if err != nil { panic(err) }
	params, err := identity.ParametersFromCoreProfile(core)
	if err != nil { panic(err) }
	main := params.BootstrappingParameters()
	residual := params.ResidualParameters()
	sk := rlwe.NewKeyGenerator(main).GenSecretKeyNew()
	start := time.Now()
	fmt.Fprintln(os.Stderr, "REFERENCE_KEYS_BEGIN", route)
	keys, err := identity.GenEvaluationKeys(context.Background(), params, sk)
	if err != nil { panic(err) }
	fmt.Fprintln(os.Stderr, "REFERENCE_KEYS_END", time.Since(start).Seconds())
	evaluator, err := identity.NewEvaluator(params, keys)
	if err != nil { panic(err) }
	encoder := ckks.NewEncoder(residual)
	encryptor := rlwe.NewEncryptor(residual, sk)
	decryptor := rlwe.NewDecryptor(residual, sk)
	values := make([]complex128, main.MaxSlots())
	for i := range values {
		values[i] = complex((float64(i%33)/32-0.5)/2,
			(float64((i/33)%33)/32-0.5)/2)
	}
	pt := ckks.NewPlaintext(residual, 2)
	pt.Scale = rlwe.NewScale(math.Exp2(40))
	if err := encoder.Encode(values, pt); err != nil { panic(err) }
	ct, err := encryptor.EncryptNew(pt)
	if err != nil { panic(err) }
	start = time.Now()
	out, err := evaluator.Bootstrap(ct, identity.RefreshIdentity)
	if err != nil { panic(err) }
	seconds := time.Since(start).Seconds()
	actual := make([]complex128, len(values))
	if err := encoder.Decode(decryptor.DecryptNew(out), actual); err != nil { panic(err) }
	maxError, signal, noise := 0.0, 0.0, 0.0
	for i := range actual {
		error := cmplx.Abs(actual[i]-values[i])
		if math.IsNaN(error) || math.IsInf(error, 0) { panic("nonfinite reference output") }
		maxError = math.Max(maxError, error)
		signal += math.Pow(cmplx.Abs(values[i]), 2)
		noise += error * error
	}
	result := map[string]any{
		"reference_revision":"6330f64b178b6215edd35baf5b11c8da558ca95c",
		"route":route.String(), "key_set":os.Args[2], "output_level":out.Level(),
		"output_log2_scale":out.Scale.Log2(), "max_complex_error":maxError,
		"snr_db":10*math.Log10(signal/noise), "untimed_diagnostic_seconds":seconds,
		"pass":out.Level()==20 && math.Abs(out.Scale.Log2()-40)<1e-9 && maxError<=math.Exp2(-18),
	}
	patterns := []any{}
	for _, name := range []string{"signed-grid", "zeros", "endpoints", "independent"} {
		for i := range values {
			x := -.25 + .5 * float64(i) / float64(len(values)-1)
			y := -.25 + .5 * float64((i*1031)%len(values)) / float64(len(values)-1)
			switch name {
			case "signed-grid":
				values[i] = complex(x, -x)
			case "zeros":
				values[i] = 0
			case "endpoints":
				x, y = .25, -.25
				if i%2 != 0 { x = -.25 }
				if i%3 != 0 { y = .25 }
				values[i] = complex(x, y)
			case "independent":
				if i%3 == 0 { x = 0 }
				if i%5 == 0 { y = 0 }
				values[i] = complex(x, y)
			}
		}
		pt := ckks.NewPlaintext(residual, 2)
		pt.Scale = rlwe.NewScale(math.Exp2(40))
		if err := encoder.Encode(values, pt); err != nil { panic(err) }
		ct, err := encryptor.EncryptNew(pt)
		if err != nil { panic(err) }
		out, err := evaluator.Bootstrap(ct, identity.RefreshIdentity)
		if err != nil { panic(err) }
		if err := encoder.Decode(decryptor.DecryptNew(out), actual); err != nil { panic(err) }
		maxError, signal, noise = 0, 0, 0
		for i := range actual {
			error := cmplx.Abs(actual[i]-values[i])
			if math.IsNaN(error) || math.IsInf(error, 0) { panic("nonfinite reference output") }
			maxError = math.Max(maxError, error)
			signal += math.Pow(cmplx.Abs(values[i]), 2)
			noise += error * error
		}
		var snr any
		if signal > 0 { snr = 10*math.Log10(signal/noise) }
		pass := out.Level()==20 && math.Abs(out.Scale.Log2()-40)<1e-9 && maxError<=math.Exp2(-18)
		result["pass"] = result["pass"].(bool) && pass
		patterns = append(patterns, map[string]any{
			"pattern":name, "max_complex_error":maxError, "snr_db":snr,
			"output_level":out.Level(), "output_log2_scale":out.Scale.Log2(), "pass":pass,
		})
		fmt.Fprintln(os.Stderr, "REFERENCE_PATTERN", name, maxError, pass)
	}
	result["patterns"] = patterns
	if err := json.NewEncoder(os.Stdout).Encode(result); err != nil { panic(err) }
}
