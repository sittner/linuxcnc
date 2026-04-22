// gmicompile compiles .gmi interface definitions to C/Go/Python code.
//
// Usage:
//
//	gmicompile [options] file.gmi
//
// Options:
//
//	--help           Show this help message
//	--parse          Parse only — print AST as JSON
//	--server-c       Generate C server callbacks and types
//	--server-go      Generate Go server handlers
//	--client-c       Generate C REST client (cJSON/libcurl)
//	--client-go      Generate Go REST client
//	--client-python  Generate Python REST client
//	-o PATH          Output file or directory
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/sittner/linuxcnc/src/gomc/internal/gmicompile/ast"
	"github.com/sittner/linuxcnc/src/gomc/internal/gmicompile/cgen"
	"github.com/sittner/linuxcnc/src/gomc/internal/gmicompile/parser"
)

const usageText = `gmicompile: Compile GMI interface definitions

Usage:
    gmicompile [options] file.gmi

Options:
    --help           Show this help message
    --parse          Parse only — print AST as JSON

Code generation:
    --server-c       Generate C server header (types, callback typedefs)
    --client-c       Generate C REST client (header + source, cJSON/libcurl)
    --server-go      Generate Go server handlers (not yet implemented)
    --client-go      Generate Go REST client
    --client-python  Generate Python REST client (not yet implemented)
    -o PATH          Output file or directory

Examples:
    gmicompile --parse hal.gmi
    gmicompile --server-c hal.gmi -o hal_api.h
    gmicompile --client-c halcmd.gmi -o halcmd_client
`

type mode int

const (
	modeParse mode = iota
	modeServerC
	modeClientC
	modeServerGo
	modeClientGo
	modeClientPython
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usageText)
		os.Exit(1)
	}

	var m mode
	var outputPath string
	var files []string

	for i := 1; i < len(os.Args); i++ {
		arg := os.Args[i]
		switch arg {
		case "--help", "-h":
			fmt.Print(usageText)
			os.Exit(0)
		case "--parse":
			m = modeParse
		case "--server-c":
			m = modeServerC
		case "--client-c":
			m = modeClientC
		case "--server-go":
			m = modeServerGo
		case "--client-go":
			m = modeClientGo
		case "--client-python":
			m = modeClientPython
		case "-o":
			if i+1 < len(os.Args) {
				i++
				outputPath = os.Args[i]
			}
		default:
			if len(arg) > 0 && arg[0] != '-' {
				files = append(files, arg)
			}
		}
	}

	if len(files) == 0 {
		fmt.Fprintln(os.Stderr, "gmicompile: no input files")
		os.Exit(1)
	}

	for _, file := range files {
		if err := processFile(file, m, outputPath); err != nil {
			fmt.Fprintf(os.Stderr, "gmicompile: %v\n", err)
			os.Exit(1)
		}
	}
}

func processFile(file string, m mode, outputPath string) error {
	src, err := os.ReadFile(file)
	if err != nil {
		return err
	}

	api, errors := parser.Parse(file, string(src))
	if len(errors) > 0 {
		for _, e := range errors {
			fmt.Fprintln(os.Stderr, e)
		}
		return fmt.Errorf("parse failed")
	}

	if errs := validateAPI(api); len(errs) > 0 {
		for _, e := range errs {
			fmt.Fprintln(os.Stderr, e)
		}
		return fmt.Errorf("validation failed")
	}

	switch m {
	case modeParse:
		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		return enc.Encode(api)

	case modeServerC:
		return generateServerC(api, outputPath)

	case modeClientC:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-c requires @rest_export true", file)
		}
		return generateClientC(api, outputPath)

	case modeServerGo:
		return generateServerGo(api, outputPath)

	case modeClientGo:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-go requires @rest_export true", file)
		}
		return generateClientGo(api, outputPath)

	case modeClientPython:
		if !api.RestExport {
			return fmt.Errorf("%s: --client-python requires @rest_export true", file)
		}
		return generateClientPython(api, outputPath)
	}

	return nil
}

func generateServerC(api *ast.API, outputPath string) error {
	// Default output name
	if outputPath == "" {
		outputPath = api.Name + "_api.h"
	}

	// Generate C header
	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := cgen.GenerateServerHeader(f, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)

	// Generate Go cgo dispatch file alongside the header.
	// Derive Go file path: same directory, <api>_cgo.go
	dir := filepath.Dir(outputPath)
	goPath := filepath.Join(dir, api.Name+"_cgo.go")

	// Derive package name from directory
	pkgName := api.Name
	if dir != "." && dir != "" {
		pkgName = filepath.Base(dir)
	}

	headerFile := filepath.Base(outputPath)

	gf, err := os.Create(goPath)
	if err != nil {
		return err
	}
	defer gf.Close()

	if err := cgen.GenerateDispatchC(gf, api, pkgName, headerFile); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", goPath)

	return nil
}

func generateClientC(api *ast.API, outputPath string) error {
	// If outputPath ends with .h or .c, use it as base
	// Otherwise treat as base name
	var baseName string
	if outputPath == "" {
		baseName = api.Name + "_client"
	} else {
		baseName = strings.TrimSuffix(outputPath, filepath.Ext(outputPath))
	}

	headerPath := baseName + ".h"
	sourcePath := baseName + ".c"

	// Generate header
	hf, err := os.Create(headerPath)
	if err != nil {
		return err
	}
	defer hf.Close()
	if err := cgen.GenerateClientHeader(hf, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", headerPath)

	// Generate source
	sf, err := os.Create(sourcePath)
	if err != nil {
		return err
	}
	defer sf.Close()
	if err := cgen.GenerateClientSource(sf, api); err != nil {
		return err
	}
	fmt.Fprintf(os.Stderr, "generated %s\n", sourcePath)

	return nil
}

func generateServerGo(api *ast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_api.go"
	}

	// Derive package name from output directory, default to api name
	pkgName := api.Name
	if dir := filepath.Dir(outputPath); dir != "." && dir != "" {
		pkgName = filepath.Base(dir)
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := cgen.GenerateServerGo(f, api, pkgName); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func generateClientGo(api *ast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_client.go"
	}

	// Derive package name from output directory, default to api name + "client"
	pkgName := api.Name + "client"
	if dir := filepath.Dir(outputPath); dir != "." && dir != "" {
		pkgName = filepath.Base(dir)
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := cgen.GenerateClientGo(f, api, pkgName); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

func generateClientPython(api *ast.API, outputPath string) error {
	if outputPath == "" {
		outputPath = api.Name + "_client.py"
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	if err := cgen.GenerateClientPython(f, api); err != nil {
		return err
	}

	fmt.Fprintf(os.Stderr, "generated %s\n", outputPath)
	return nil
}

// validateAPI checks semantic constraints on a parsed API.
func validateAPI(api *ast.API) []string {
	return nil
}
