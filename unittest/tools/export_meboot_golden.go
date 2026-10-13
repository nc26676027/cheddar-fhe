// Run from the independent paper1-meboot module so its pinned Lattigo fork
// supplies the reference, rather than translating the C++ planner.
package main

import (
	"compress/gzip"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"sort"

	"github.com/tuneinsight/lattigo/v6/circuits/ckks/dft"
)

func main() {
	output := flag.String("output", "meboot_golden_4434.bin.gz", "output fixture")
	flag.Parse()
	f, err := os.Create(*output)
	if err != nil {
		panic(err)
	}
	z := gzip.NewWriter(f)
	write := func(v any) {
		if err := binary.Write(z, binary.LittleEndian, v); err != nil {
			panic(err)
		}
	}
	write([8]byte{'M', 'E', 'B', 'C', 'T', 'S', 'G', '1'})
	write(uint32(1 << 15))
	write(uint32(4))
	literal := dft.MatrixLiteral{
		Type: dft.HomomorphicEncode, Format: dft.RepackImagAsReal,
		LogSlots: 15, Levels: []int{1, 1, 1, 1},
		CustomedMerge: []int{4, 4, 3, 4},
	}
	count := 0
	// Generate one factor at a time to bound arbitrary-precision memory use.
	for stage := 0; stage < 4; stage++ {
		matrix := literal.GenMatricesSlice(16, 128, stage, 1)[0]
		offsets := make([]int, 0, len(matrix))
		for offset := range matrix {
			offsets = append(offsets, offset)
		}
		sort.Ints(offsets)
		write(uint32(len(offsets)))
		for _, offset := range offsets {
			write(uint32(offset))
			for _, value := range matrix[offset] {
				re, _ := value[0].Float64()
				im, _ := value[1].Float64()
				write([2]float64{re, im})
				count++
			}
		}
		fmt.Printf("stage %d: %d diagonals, 32768 complex coefficients each\n", stage+1, len(offsets))
	}
	if err := z.Close(); err != nil {
		panic(err)
	}
	if err := f.Close(); err != nil {
		panic(err)
	}
	fmt.Printf("exported %d coefficients to %s\n", count, *output)
}
