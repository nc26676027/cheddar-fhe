// Run from the pinned paper1-linear-bootstrap/meboot module with Go >= 1.21.
// This exports the unretuned polynomial; the Python importer binds native gains.
package main

import (
	"encoding/json"
	"os"

	"github.com/tuneinsight/lattigo/v6/circuits/ckks/mod1"
	"paper1-meboot/identity"
	"paper1-meboot/identity/presets"
)

func main() {
	profile, err := presets.Identity19MEBOOT(identity.RouteJoint4434)
	if err != nil {
		panic(err)
	}
	literal := profile.Parameters.Mod1ParametersLiteral
	literal.K, literal.Mod1Degree, literal.DoubleAngle = 20, 63, 2
	polynomial, err := mod1.NewParametersFromLiteral(profile.Parameters.BootstrappingParameters, literal)
	if err != nil {
		panic(err)
	}
	coefficients := make([][2]float64, len(polynomial.Mod1Poly.Coeffs))
	for i, coefficient := range polynomial.Mod1Poly.Coeffs {
		if coefficient != nil {
			value := coefficient.Complex128()
			coefficients[i] = [2]float64{real(value), imag(value)}
		}
	}
	record := map[string]any{
		"mod1":                        literal,
		"mod1_actual_degree":          polynomial.Mod1Poly.Degree(),
		"mod1_coefficients_float64":   coefficients,
		"mod1_interval_a":             polynomial.Mod1Poly.A.Text('g', 30),
		"mod1_interval_b":             polynomial.Mod1Poly.B.Text('g', 30),
		"mod1_sqrt2pi":                polynomial.Sqrt2Pi,
		"mod1_power_basis_scale_lift": profile.Mod1PowerBasisScaleLift,
		"mod1_q_diff":                 polynomial.QDiff,
	}
	encoder := json.NewEncoder(os.Stdout)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(record); err != nil {
		panic(err)
	}
}
