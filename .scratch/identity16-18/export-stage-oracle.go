package main

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"math"
	"math/big"
	"os"
	"sort"

	"github.com/tuneinsight/lattigo/v6/circuits/ckks/dft"
	"paper1-meboot/identity"
	"paper1-meboot/identity/presets"
)

// Export unencoded selected matrices one factor at a time. This fixture has
// no dependency on the Cheddar planner or the retained Golden manifest.
func main() {
	out := map[string]any{"reference_revision": "6330f64b178b6215edd35baf5b11c8da558ca95c"}
	for _, item := range []struct {
		name string
		route identity.Route
	}{{"joint4434", identity.RouteJoint4434}, {"ordinary", identity.RouteOrdinary}} {
		p, err := presets.Identity16Selected(item.route)
		if err != nil { panic(err) }
		main := p.Parameters.BootstrappingParameters
		values := make([]complex128, main.MaxSlots())
		for i := range values {
			values[i] = complex((float64(i%33)/32-0.5)/2,
				(float64((i/33)%33)/32-0.5)/2)
		}
		stc := p.Parameters.SlotsToCoeffsParameters
		stc.Scaling = new(big.Float).SetFloat64(1.0/64)
		cts := p.Parameters.CoeffsToSlotsParameters
		cts.Scaling = new(big.Float).SetFloat64(1/(p.Polynomial.K*p.Polynomial.QDiff))
		with := p.Parameters
		with.EvalRound = true
		bypass := with.GetC2SBypass()
		bypass.Scaling = new(big.Float).SetFloat64(0.25)
		route := map[string]any{}
		for _, group := range []struct{name string; literal dft.MatrixLiteral}{
			{"stc", stc}, {"cts", cts}, {"bypass", bypass},
		} {
			current := append([]complex128(nil), values...)
			stages := []any{}
			for layer := 0; layer < group.literal.Depth(false); layer++ {
				fmt.Fprintf(os.Stderr, "%s %s factor %d\n", item.name, group.name, layer)
				matrix := group.literal.GenMatricesSlice(main.LogN(), main.EncodingPrecision(), layer, 1)[0]
				next := make([]complex128, len(current))
				hash := sha256.New()
				maxDiagonal := 0.0
				indices := matrix.DiagonalsIndexList()
				sort.Ints(indices)
				sample := []any{}
				for _, rotation := range indices {
					diag := matrix[rotation]
					var buffer [16]byte
					for j, coefficient := range diag {
						v := coefficient.Complex128()
						binary.LittleEndian.PutUint64(buffer[:8], math.Float64bits(real(v)))
						binary.LittleEndian.PutUint64(buffer[8:], math.Float64bits(imag(v)))
						hash.Write(buffer[:])
						maxDiagonal = math.Max(maxDiagonal, math.Hypot(real(v), imag(v)))
						source := (j+rotation+len(current))%len(current)
						next[j] += v * current[source]
						if j < 4 {
							sample = append(sample, []any{rotation,j,real(v),imag(v)})
						}
					}
				}
				stageValues := make([][2]float64,len(next))
				for j,v := range next { stageValues[j] = [2]float64{real(v),imag(v)} }
				stages = append(stages, map[string]any{
					"factor":layer, "supports":indices, "diagonal_float64_sha256":hex.EncodeToString(hash.Sum(nil)),
					"maximum_diagonal_magnitude":maxDiagonal, "coefficient_samples":sample,
					"signed_grid_values":stageValues,
				})
				current = next
			}
			route[group.name] = stages
		}
		out[item.name] = route
	}
	if err := json.NewEncoder(os.Stdout).Encode(out); err != nil { panic(err) }
}
