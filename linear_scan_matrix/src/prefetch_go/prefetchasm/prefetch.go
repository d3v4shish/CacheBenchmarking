// Package prefetchasm contains the explicit one-instruction Go prefetch
// helper. It is separate from the cgo package because Go 1.26 rejects Go
// assembly files in a package that imports C.
package prefetchasm

import "unsafe"

//go:noescape
func T2(addr unsafe.Pointer)
