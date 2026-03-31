// Package cgen generates C source code from a modcompile AST.
//
// The generated code targets the gomc cmod API (gomc_env.h) and produces
// a single .c file that compiles to one .so plugin for linuxcnc-launcher.
package cgen

import (
	"fmt"
	"io"
	"strings"

	"github.com/sittner/linuxcnc/src/launcher/internal/modcompile/ast"
)

// Generate writes the complete C source for pkg to w.
func Generate(w io.Writer, pkg *ast.Package) error {
	g := &generator{w: w, comp: &pkg.Component}
	return g.generate()
}

// ---------------------------------------------------------------------------
// Internal generator
// ---------------------------------------------------------------------------

type generator struct {
	w    io.Writer
	comp *ast.Component
	err  error
}

func (g *generator) generate() error {
	g.emitHeader()
	g.emitInstanceStruct()
	g.emitFunctionForwards()
	g.emitConvenienceDefines()
	g.emitUserCode()
	g.emitUndefConvenience()
	g.emitStartStopDestroy()
	g.emitNew()
	return g.err
}

// printf writes formatted text, tracking any write error.
func (g *generator) printf(format string, args ...interface{}) {
	if g.err != nil {
		return
	}
	_, g.err = fmt.Fprintf(g.w, format, args...)
}

// ---------------------------------------------------------------------------
// Name mangling
// ---------------------------------------------------------------------------

// toC converts a HAL-style name to a valid C identifier.
// Strips # characters and adjacent separators, replaces -, . with _,
// collapses multiple _.
func toC(halName string) string {
	// Strip runs of # and their adjacent separators.
	s := stripHashMarkers(halName)
	// Replace . and - with _.
	var b strings.Builder
	prevUnderscore := false
	for _, c := range s {
		switch c {
		case '-', '.', '_':
			if !prevUnderscore {
				b.WriteByte('_')
				prevUnderscore = true
			}
		default:
			b.WriteRune(c)
			prevUnderscore = false
		}
	}
	return b.String()
}

// stripHashMarkers removes runs of # and their directly-adjacent separators.
// For middle ##: "joint.##.offset" → "joint.offset" (keep one separator).
// For trailing ##: "x-val-##" → "x-val".
func stripHashMarkers(s string) string {
	isSep := func(c byte) bool {
		return c == '-' || c == '_' || c == '.'
	}

	var b strings.Builder
	i := 0
	for i < len(s) {
		if isSep(s[i]) && i+1 < len(s) && s[i+1] == '#' {
			// Separator before a hash run — consume sep + all hashes.
			leadingSep := s[i]
			i++ // skip separator
			for i < len(s) && s[i] == '#' {
				i++
			}
			// If there's a trailing separator AND more content after, keep one separator.
			if i < len(s) && isSep(s[i]) && i+1 < len(s) {
				b.WriteByte(leadingSep)
				i++ // skip trailing separator
			}
		} else if s[i] == '#' {
			// Hash at start of string (no leading sep).
			for i < len(s) && s[i] == '#' {
				i++
			}
			if i < len(s) && isSep(s[i]) {
				i++
			}
		} else {
			b.WriteByte(s[i])
			i++
		}
	}
	return b.String()
}

// toHALFmt converts a HAL name to a printf format string for pin/param names.
// Underscores → hyphens, ## → %0Nd (anywhere in the name).
func toHALFmt(halName string) string {
	// Replace _ with -.
	s := strings.ReplaceAll(halName, "_", "-")

	// Find and replace runs of # with %0Nd.
	var b strings.Builder
	i := 0
	hasHash := false
	for i < len(s) {
		if s[i] == '#' {
			hasHash = true
			count := 0
			for i < len(s) && s[i] == '#' {
				count++
				i++
			}
			fmt.Fprintf(&b, "%%0%dd", count)
		} else {
			b.WriteByte(s[i])
			i++
		}
	}
	if !hasHash {
		// No array — strip trailing separators for scalar names.
		return strings.TrimRight(b.String(), "-.")
	}
	return b.String()
}

// ---------------------------------------------------------------------------
// HAL type/direction helpers
// ---------------------------------------------------------------------------

func halTypeEnum(t ast.HALType) string {
	switch t {
	case ast.HALBit:
		return "GOMC_HAL_BIT"
	case ast.HALFloat:
		return "GOMC_HAL_FLOAT"
	case ast.HALS32:
		return "GOMC_HAL_S32"
	case ast.HALU32:
		return "GOMC_HAL_U32"
	case ast.HALPort:
		return "GOMC_HAL_PORT"
	default:
		return "GOMC_HAL_BIT"
	}
}

func pinDirEnum(d ast.PinDir) string {
	switch d {
	case ast.PinIn:
		return "GOMC_HAL_IN"
	case ast.PinOut:
		return "GOMC_HAL_OUT"
	case ast.PinIO:
		return "GOMC_HAL_IO"
	default:
		return "GOMC_HAL_IN"
	}
}

func paramDirEnum(d ast.ParamDir) string {
	switch d {
	case ast.ParamR:
		return "GOMC_HAL_RO"
	case ast.ParamRW:
		return "GOMC_HAL_RW"
	default:
		return "GOMC_HAL_RO"
	}
}

func pinTypedNewf(t ast.HALType) string {
	switch t {
	case ast.HALBit:
		return "gomc_hal_pin_bit_newf"
	case ast.HALFloat:
		return "gomc_hal_pin_float_newf"
	case ast.HALS32:
		return "gomc_hal_pin_s32_newf"
	case ast.HALU32:
		return "gomc_hal_pin_u32_newf"
	case ast.HALPort:
		return "gomc_hal_pin_port_newf"
	default:
		return "gomc_hal_pin_bit_newf"
	}
}

func paramTypedNewf(t ast.HALType) string {
	switch t {
	case ast.HALBit:
		return "gomc_hal_param_bit_newf"
	case ast.HALFloat:
		return "gomc_hal_param_float_newf"
	case ast.HALS32:
		return "gomc_hal_param_s32_newf"
	case ast.HALU32:
		return "gomc_hal_param_u32_newf"
	default:
		return "gomc_hal_param_bit_newf"
	}
}

// ---------------------------------------------------------------------------
// Feature detection helpers
// ---------------------------------------------------------------------------

func (g *generator) hasPersonality() bool {
	for _, p := range g.comp.Pins {
		if p.Personality != "" || p.ArrayPersonality != "" {
			return true
		}
	}
	for _, p := range g.comp.Params {
		if p.Personality != "" || p.ArrayPersonality != "" {
			return true
		}
	}
	return false
}

func (g *generator) isUserspace() bool {
	return g.comp.Options["userspace"] != ""
}

func (g *generator) hasExtraSetup() bool {
	return g.comp.Options["extra_setup"] != ""
}

func (g *generator) hasExtraCleanup() bool {
	return g.comp.Options["extra_cleanup"] != ""
}

func (g *generator) compType() string {
	if g.isUserspace() {
		return "GOMC_HAL_COMP_USER"
	}
	return "GOMC_HAL_COMP_REALTIME"
}

// ---------------------------------------------------------------------------
// Code emission
// ---------------------------------------------------------------------------

func (g *generator) emitHeader() {
	g.printf("/* Autogenerated by modcompile — do not edit. */\n\n")
	g.printf("#include \"gomc_env.h\"\n")
	g.printf("#include <stdlib.h>\n")
	g.printf("#include <string.h>\n")
	g.printf("#include <stdbool.h>\n")
	g.printf("\n#ifndef TRUE\n#define TRUE 1\n#endif\n")
	g.printf("#ifndef FALSE\n#define FALSE 0\n#endif\n")

	for _, inc := range g.comp.Includes {
		g.printf("#include %s\n", inc)
	}
	g.printf("\n")
}

func (g *generator) needsHALStruct() bool {
	return len(g.comp.Pins) > 0 || len(g.comp.Params) > 0
}

func (g *generator) emitInstanceStruct() {
	g.printf("/* ---------------------------------------------------------------------------\n")
	g.printf(" * Instance data\n")
	g.printf(" * ------------------------------------------------------------------------- */\n\n")

	// inst_hal_t lives in HAL shared memory (allocated via hal_malloc).
	// Pin pointers and param values MUST reside in shmem for hal_pin_new /
	// hal_param_new to accept the data_ptr_addr.
	if g.needsHALStruct() {
		g.printf("typedef struct {\n")

		// Pins — pointers to HAL shared memory.
		for _, pin := range g.comp.Pins {
			cName := toC(pin.Name)
			cType := pin.Type.CType()
			if pin.ArraySize > 0 {
				g.printf("    %s *%s[%d];\n", cType, cName, pin.ArraySize)
			} else {
				g.printf("    %s *%s;\n", cType, cName)
			}
		}

		// Params — value storage.
		for _, param := range g.comp.Params {
			cName := toC(param.Name)
			cType := param.Type.CType()
			if param.ArraySize > 0 {
				g.printf("    %s %s[%d];\n", cType, cName, param.ArraySize)
			} else {
				g.printf("    %s %s;\n", cType, cName)
			}
		}

		g.printf("} inst_hal_t;\n\n")
	}

	// inst_t lives on the regular heap (calloc).  It holds the cmod
	// lifecycle vtable, env pointer, and a pointer to the shmem portion.
	g.printf("typedef struct {\n")
	g.printf("    cmod_t base;\n")
	g.printf("    const cmod_env_t *env;\n")
	g.printf("    int comp_id;\n")
	g.printf("    char name[GOMC_HAL_NAME_LEN + 1];\n")

	if g.needsHALStruct() {
		g.printf("    inst_hal_t *hal;  /* HAL shared memory portion */\n")
	}

	if g.hasPersonality() {
		g.printf("    int _personality;\n")
	}

	// Variables.
	for _, v := range g.comp.Variables {
		if v.Array > 0 {
			g.printf("    %s %s[%d];\n", v.CType, v.Name, v.Array)
		} else {
			g.printf("    %s %s;\n", v.CType, v.Name)
		}
	}

	// Option data — extra allocation.
	if dataType, ok := g.comp.Options["data"]; ok {
		g.printf("    %s _data;\n", dataType)
	}

	g.printf("} inst_t;\n\n")
}

func (g *generator) emitFunctionForwards() {
	for _, fn := range g.comp.Functions {
		cName := funcCName(fn.Name)
		g.printf("static void %s(void *arg, long period);\n", cName)
	}
	if g.hasExtraSetup() {
		g.printf("static int extra_setup(inst_t *__comp_inst, const char *name, int _pers);\n")
	}
	if g.hasExtraCleanup() {
		g.printf("static void extra_cleanup(void);\n")
	}
	g.printf("\n")
}

func funcCName(name string) string {
	return "funct_" + toC(name)
}

func (g *generator) emitConvenienceDefines() {
	g.printf("/* ---------------------------------------------------------------------------\n")
	g.printf(" * Convenience defines — user code accesses pins/params by short names.\n")
	g.printf(" * ------------------------------------------------------------------------- */\n\n")

	// FUNCTION macro — wraps user code into the real function signature.
	g.printf("#undef FUNCTION\n")
	g.printf("#define FUNCTION(name_) \\\n")
	g.printf("    static void funct_ ## name_ ## _body(inst_t *__comp_inst, long period); \\\n")
	g.printf("    static void funct_ ## name_(void *arg, long period) { \\\n")
	g.printf("        funct_ ## name_ ## _body((inst_t *)arg, period); \\\n")
	g.printf("    } \\\n")
	g.printf("    static void funct_ ## name_ ## _body(inst_t *__comp_inst, long period)\n\n")

	// fperiod convenience.
	g.printf("#define fperiod (period * 1e-9)\n\n")

	// Pin convenience: scalar → dereference pointer.  Pins live in inst_hal_t.
	// Array pin → function-like macro with index.
	for _, pin := range g.comp.Pins {
		cName := toC(pin.Name)
		if pin.ArraySize > 0 {
			g.printf("#define %s(i) (*(__comp_inst->hal->%s[i]))\n", cName, cName)
		} else {
			g.printf("#define %s (*(__comp_inst->hal->%s))\n", cName, cName)
		}
	}

	// Param convenience: direct access (not pointer).  Params live in inst_hal_t.
	for _, param := range g.comp.Params {
		cName := toC(param.Name)
		if param.ArraySize > 0 {
			g.printf("#define %s(i) (__comp_inst->hal->%s[i])\n", cName, cName)
		} else {
			g.printf("#define %s (__comp_inst->hal->%s)\n", cName, cName)
		}
	}

	// Variable convenience.
	for _, v := range g.comp.Variables {
		name := v.Name
		// Strip leading * for pointer variables in the define.
		defName := strings.TrimLeft(name, "*")
		if v.Array > 0 {
			g.printf("#define %s (__comp_inst->%s)\n", defName, defName)
		} else {
			g.printf("#define %s (__comp_inst->%s)\n", defName, name)
		}
	}

	// Personality convenience.
	if g.hasPersonality() {
		g.printf("#define personality (__comp_inst->_personality)\n")
	}

	// Data convenience.
	if _, ok := g.comp.Options["data"]; ok {
		g.printf("#define data (__comp_inst->_data)\n")
	}

	// EXTRA_SETUP macro.
	if g.hasExtraSetup() {
		g.printf("\n#undef EXTRA_SETUP\n")
		g.printf("#define EXTRA_SETUP() \\\n")
		g.printf("    static int extra_setup(inst_t *__comp_inst, const char *name, int _pers)\n")
	}

	// EXTRA_CLEANUP macro.
	if g.hasExtraCleanup() {
		g.printf("\n#undef EXTRA_CLEANUP\n")
		g.printf("#define EXTRA_CLEANUP() static void extra_cleanup(void)\n")
	}

	g.printf("\n")
}

func (g *generator) needsAutoWrap() bool {
	if g.comp.VerbatimC == "" {
		return false
	}
	if strings.Contains(g.comp.VerbatimC, "FUNCTION(") {
		return false
	}
	// Auto-wrap when exactly one function is declared and user code
	// doesn't contain an explicit FUNCTION() invocation.
	return len(g.comp.Functions) == 1
}

func (g *generator) emitUserCode() {
	g.printf("/* ---------------------------------------------------------------------------\n")
	g.printf(" * User code\n")
	g.printf(" * ------------------------------------------------------------------------- */\n\n")
	if g.comp.VerbatimC == "" {
		return
	}
	if g.needsAutoWrap() {
		g.printf("FUNCTION(%s){\n%s\n}\n\n", g.comp.Functions[0].Name, g.comp.VerbatimC)
	} else {
		g.printf("%s\n\n", g.comp.VerbatimC)
	}
}

func (g *generator) emitUndefConvenience() {
	g.printf("/* ---------------------------------------------------------------------------\n")
	g.printf(" * Undefine convenience macros — clean namespace for generated code below.\n")
	g.printf(" * ------------------------------------------------------------------------- */\n\n")

	g.printf("#undef FUNCTION\n")
	g.printf("#undef fperiod\n")
	for _, pin := range g.comp.Pins {
		g.printf("#undef %s\n", toC(pin.Name))
	}
	for _, param := range g.comp.Params {
		g.printf("#undef %s\n", toC(param.Name))
	}
	for _, v := range g.comp.Variables {
		g.printf("#undef %s\n", strings.TrimLeft(v.Name, "*"))
	}
	if g.hasPersonality() {
		g.printf("#undef personality\n")
	}
	if _, ok := g.comp.Options["data"]; ok {
		g.printf("#undef data\n")
	}
	if g.hasExtraSetup() {
		g.printf("#undef EXTRA_SETUP\n")
	}
	if g.hasExtraCleanup() {
		g.printf("#undef EXTRA_CLEANUP\n")
	}
	g.printf("\n")
}

func (g *generator) emitStartStopDestroy() {
	g.printf("/* ---------------------------------------------------------------------------\n")
	g.printf(" * Lifecycle: Start / Stop / Destroy\n")
	g.printf(" * ------------------------------------------------------------------------- */\n\n")

	g.printf("static int inst_start(cmod_t *self) {\n")
	g.printf("    (void)self;\n")
	g.printf("    return 0;\n")
	g.printf("}\n\n")

	g.printf("static void inst_stop(cmod_t *self) {\n")
	g.printf("    (void)self;\n")
	g.printf("}\n\n")

	g.printf("static void inst_destroy(cmod_t *self) {\n")
	g.printf("    inst_t *inst = (inst_t *)self;\n")
	if g.hasExtraCleanup() {
		g.printf("    extra_cleanup();\n")
	}
	g.printf("    if (inst->comp_id > 0)\n")
	g.printf("        inst->env->hal->exit(inst->env->hal->ctx, inst->comp_id);\n")
	g.printf("    inst->env->rtapi->free(inst->env->rtapi->ctx, inst);\n")
	g.printf("}\n\n")
}

func (g *generator) emitNew() {
	g.printf("/* ---------------------------------------------------------------------------\n")
	g.printf(" * Constructor: New()\n")
	g.printf(" * ------------------------------------------------------------------------- */\n\n")

	g.printf("int New(const cmod_env_t *env, const char *name,\n")
	g.printf("        int argc, const char **argv, cmod_t **out) {\n")
	g.printf("    int r;\n")
	if g.hasArrayPins() || g.hasArrayParams() {
		g.printf("    int j;\n")
	}
	g.printf("\n")

	// Allocate instance (rtapi_calloc: mlock'd, page-faulted).
	g.printf("    inst_t *inst = (inst_t *)env->rtapi->calloc(env->rtapi->ctx,\n")
	g.printf("                        sizeof(inst_t));\n")
	g.printf("    if (!inst) return -1;\n\n")

	// Wire vtable.
	g.printf("    inst->base.Start   = inst_start;\n")
	g.printf("    inst->base.Stop    = inst_stop;\n")
	g.printf("    inst->base.Destroy = inst_destroy;\n")
	g.printf("    inst->env = env;\n")
	g.printf("    snprintf(inst->name, sizeof(inst->name), \"%%s\", name);\n\n")

	// Parse personality from argv if needed.
	if g.hasPersonality() {
		g.printf("    /* Parse personality from argv: personality=N */\n")
		g.printf("    for (int i = 0; i < argc; i++) {\n")
		g.printf("        if (strncmp(argv[i], \"personality=\", 12) == 0)\n")
		g.printf("            inst->_personality = atoi(argv[i] + 12);\n")
		g.printf("    }\n\n")
	}

	// Parse modparams from argv.
	for _, mp := range g.comp.Modparams {
		if mp.Type == "dummy" {
			continue
		}
		g.printf("    /* modparam: %s */\n", mp.Name)
		if mp.Default != "" {
			g.printf("    int mp_%s = %s;\n", mp.Name, mp.Default)
		} else {
			g.printf("    int mp_%s = 0;\n", mp.Name)
		}
		g.printf("    for (int i = 0; i < argc; i++) {\n")
		g.printf("        if (strncmp(argv[i], \"%s=\", %d) == 0)\n", mp.Name, len(mp.Name)+1)
		g.printf("            mp_%s = atoi(argv[i] + %d);\n", mp.Name, len(mp.Name)+1)
		g.printf("    }\n")
		g.printf("    (void)mp_%s;\n\n", mp.Name)
	}

	// HAL init.
	g.printf("    inst->comp_id = env->hal->init(env->hal->ctx, name,\n")
	g.printf("                                   env->dl_handle, %s);\n", g.compType())
	g.printf("    if (inst->comp_id < 0) { env->rtapi->free(env->rtapi->ctx, inst); return -1; }\n\n")

	// Allocate HAL shared memory portion for pins and params.
	if g.needsHALStruct() {
		g.printf("    inst->hal = (inst_hal_t *)env->hal->malloc(env->hal->ctx,\n")
		g.printf("                    sizeof(inst_hal_t));\n")
		g.printf("    if (!inst->hal) goto err;\n")
		g.printf("    memset(inst->hal, 0, sizeof(inst_hal_t));\n\n")
	}

	// Extra setup (runs before pins, can modify personality).
	if g.hasExtraSetup() {
		g.printf("    r = extra_setup(inst, name, inst->_personality);\n")
		g.printf("    if (r != 0) goto err;\n\n")
	}

	// Create pins.
	for _, pin := range g.comp.Pins {
		g.emitPinCreation(pin)
	}

	// Create params.
	for _, param := range g.comp.Params {
		g.emitParamCreation(param)
	}

	// Set variable defaults.
	for _, v := range g.comp.Variables {
		if v.Default != "" {
			defName := strings.TrimLeft(v.Name, "*")
			if v.Array > 0 {
				g.printf("    for (int _i = 0; _i < %d; _i++)\n", v.Array)
				g.printf("        inst->%s[_i] = %s;\n", defName, v.Default)
			} else {
				g.printf("    inst->%s = %s;\n", defName, v.Default)
			}
		}
	}
	if len(g.comp.Variables) > 0 {
		g.printf("\n")
	}

	// Export functions.
	for _, fn := range g.comp.Functions {
		g.emitFunctionExport(fn)
	}

	// hal_ready.
	g.printf("    r = env->hal->ready(env->hal->ctx, inst->comp_id);\n")
	g.printf("    if (r != 0) goto err;\n\n")

	g.printf("    *out = &inst->base;\n")
	g.printf("    return 0;\n\n")

	g.printf("err:\n")
	g.printf("    env->hal->exit(env->hal->ctx, inst->comp_id);\n")
	g.printf("    env->rtapi->free(env->rtapi->ctx, inst);\n")
	g.printf("    return -1;\n")
	g.printf("}\n")
}

func (g *generator) emitPinCreation(pin ast.Pin) {
	cName := toC(pin.Name)
	halFmt := toHALFmt(pin.Name)
	newf := pinTypedNewf(pin.Type)
	dir := pinDirEnum(pin.Dir)

	// In generated New() code, personality references use inst->_personality.
	persExpr := func(expr string) string {
		return strings.ReplaceAll(expr, "personality", "inst->_personality")
	}

	openCond := ""
	closeCond := ""
	if pin.Personality != "" {
		openCond = fmt.Sprintf("    if (%s) {\n", persExpr(pin.Personality))
		closeCond = "    }\n"
	}

	if pin.ArraySize > 0 {
		// Array pin.
		if pin.Personality != "" {
			g.printf("%s", openCond)
		}
		// Personality-bounded array: bounds check.
		if pin.ArrayPersonality != "" {
			arrPers := persExpr(pin.ArrayPersonality)
			g.printf("    if ((%s) > %d) {\n", arrPers, pin.ArraySize)
			g.printf("        gomc_log_errorf(env->log, name,\n")
			g.printf("            \"Pin %s: requested size %%d exceeds max %d\",\n", pin.Name, pin.ArraySize)
			g.printf("            (int)(%s));\n", arrPers)
			g.printf("        goto err;\n")
			g.printf("    }\n")
			g.printf("    for (j = 0; j < (%s); j++) {\n", arrPers)
		} else {
			g.printf("    for (j = 0; j < %d; j++) {\n", pin.ArraySize)
		}
		g.printf("        r = %s(env->hal, %s,\n", newf, dir)
		g.printf("                &inst->hal->%s[j], inst->comp_id,\n", cName)
		g.printf("                \"%%s.%s\", name, j);\n", halFmt)
		g.printf("        if (r != 0) goto err;\n")
		if pin.Default != "" {
			g.printf("        *(inst->hal->%s[j]) = %s;\n", cName, pin.Default)
		}
		g.printf("    }\n")
		if pin.Personality != "" {
			g.printf("%s", closeCond)
		}
	} else {
		// Scalar pin.
		if pin.Personality != "" {
			g.printf("%s", openCond)
		}
		g.printf("    r = %s(env->hal, %s,\n", newf, dir)
		g.printf("            &inst->hal->%s, inst->comp_id,\n", cName)
		g.printf("            \"%%s.%s\", name);\n", halFmt)
		g.printf("    if (r != 0) goto err;\n")
		if pin.Default != "" {
			g.printf("    *(inst->hal->%s) = %s;\n", cName, pin.Default)
		}
		if pin.Personality != "" {
			g.printf("%s", closeCond)
		}
	}
	g.printf("\n")
}

func (g *generator) emitParamCreation(param ast.Param) {
	cName := toC(param.Name)
	halFmt := toHALFmt(param.Name)
	newf := paramTypedNewf(param.Type)
	dir := paramDirEnum(param.Dir)

	// In generated New() code, personality references use inst->_personality.
	persExpr := func(expr string) string {
		return strings.ReplaceAll(expr, "personality", "inst->_personality")
	}

	openCond := ""
	closeCond := ""
	if param.Personality != "" {
		openCond = fmt.Sprintf("    if (%s) {\n", persExpr(param.Personality))
		closeCond = "    }\n"
	}

	// Set default before registration (matches halcompile behavior for params).
	if param.Default != "" && param.ArraySize == 0 {
		if param.Personality != "" {
			g.printf("%s", openCond)
		}
		g.printf("    inst->hal->%s = %s;\n", cName, param.Default)
		if param.Personality != "" {
			g.printf("%s", closeCond)
		}
	}

	if param.ArraySize > 0 {
		if param.Personality != "" {
			g.printf("%s", openCond)
		}
		if param.ArrayPersonality != "" {
			arrPers := persExpr(param.ArrayPersonality)
			g.printf("    if ((%s) > %d) {\n", arrPers, param.ArraySize)
			g.printf("        gomc_log_errorf(env->log, name,\n")
			g.printf("            \"Param %s: requested size %%d exceeds max %d\",\n", param.Name, param.ArraySize)
			g.printf("            (int)(%s));\n", arrPers)
			g.printf("        goto err;\n")
			g.printf("    }\n")
			g.printf("    for (j = 0; j < (%s); j++) {\n", arrPers)
		} else {
			g.printf("    for (j = 0; j < %d; j++) {\n", param.ArraySize)
		}
		g.printf("        r = %s(env->hal, %s,\n", newf, dir)
		g.printf("                &inst->hal->%s[j], inst->comp_id,\n", cName)
		g.printf("                \"%%s.%s\", name, j);\n", halFmt)
		g.printf("        if (r != 0) goto err;\n")
		g.printf("    }\n")
		if param.Personality != "" {
			g.printf("%s", closeCond)
		}
	} else {
		if param.Personality != "" {
			g.printf("%s", openCond)
		}
		g.printf("    r = %s(env->hal, %s,\n", newf, dir)
		g.printf("            &inst->hal->%s, inst->comp_id,\n", cName)
		g.printf("            \"%%s.%s\", name);\n", halFmt)
		g.printf("    if (r != 0) goto err;\n")
		if param.Personality != "" {
			g.printf("%s", closeCond)
		}
	}
	g.printf("\n")
}

func (g *generator) emitFunctionExport(fn ast.Function) {
	cName := funcCName(fn.Name)
	fp := 1
	if !fn.FP {
		fp = 0
	}

	// Build the HAL function name: "instname" for default "_", "instname.funcname" otherwise.
	g.printf("    {\n")
	g.printf("        char fname[GOMC_HAL_NAME_LEN + 1];\n")
	if fn.Name == "_" {
		g.printf("        snprintf(fname, sizeof(fname), \"%%s\", name);\n")
	} else {
		g.printf("        snprintf(fname, sizeof(fname), \"%%s.%s\", name);\n", fn.Name)
	}
	g.printf("        r = env->hal->export_funct(env->hal->ctx, fname,\n")
	g.printf("                %s, inst, %d, 0, inst->comp_id);\n", cName, fp)
	g.printf("        if (r != 0) goto err;\n")
	g.printf("    }\n\n")
}

func (g *generator) hasArrayPins() bool {
	for _, p := range g.comp.Pins {
		if p.ArraySize > 0 {
			return true
		}
	}
	return false
}

func (g *generator) hasArrayParams() bool {
	for _, p := range g.comp.Params {
		if p.ArraySize > 0 {
			return true
		}
	}
	return false
}
