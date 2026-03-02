package main

import (
	"bufio"
	"fmt"
	"io"
	"strings"
)

// PinDir is the direction of a HAL pin from the component's perspective.
type PinDir string

const (
	// DirIn means the HAL component receives this value (HMI writes TO the PLC/HAL).
	DirIn PinDir = "in"
	// DirOut means the HAL component produces this value (HMI reads FROM the PLC/HAL).
	DirOut PinDir = "out"
)

// ConfigPin describes a single leaf symbol that maps to a HAL pin.
type ConfigPin struct {
	// Dir is the HAL pin direction ("in" or "out").
	Dir PinDir
	// HALPath is the dot-separated path for the HAL pin name, e.g.
	// "stDISPLAY_DATA.stPOOL.1.bReady".
	HALPath string
	// ADSName is the full ADS symbol name with bracket notation, e.g.
	// "stDISPLAY_DATA.stPOOL[1].bReady".
	ADSName string
	// TypeName is the ADS/TwinCAT type name, e.g. "BOOL", "DINT", "STRING(32)".
	TypeName string
}

// ConfigActionKind identifies the type of a ConfigAction.
type ConfigActionKind int

const (
	// ConfigActionPin is a leaf symbol that maps to a HAL pin.
	ConfigActionPin ConfigActionKind = 0
	// ConfigActionBeginContainer marks the start of a struct or array-element
	// container.  Alignment is the natural alignment of the container (max of its
	// members' alignments).
	ConfigActionBeginContainer ConfigActionKind = 1
	// ConfigActionEndContainer marks the end of a struct or array-element
	// container.  Alignment matches the corresponding ConfigActionBeginContainer.
	ConfigActionEndContainer ConfigActionKind = 2
)

// ConfigAction is one step in the ordered sequence produced by ParseConfigActions.
// BeginContainer / EndContainer bracket a set of ConfigActionPin entries so that
// the symbol table can apply correct struct-start and struct-end padding.
type ConfigAction struct {
	// Kind identifies whether this is a leaf pin or a container boundary.
	Kind ConfigActionKind
	// Pin is populated for ConfigActionPin actions.
	Pin ConfigPin
	// Alignment is the natural alignment in bytes for this action:
	//   ConfigActionPin:            the type's natural alignment
	//   ConfigActionBeginContainer: the container's alignment (max of members)
	//   ConfigActionEndContainer:   the container's alignment (same as Begin)
	Alignment uint32
	// ADSName is the full ADS name of the container, populated for
	// ConfigActionBeginContainer and ConfigActionEndContainer actions.
	// Example: "DISPLAY_DATA.stData.aPools[1].stMsg"
	ADSName string
}

// alignmentForType returns the natural alignment in bytes for the given ADS type
// name (already normalized to upper case). Unknown types default to 1.
func alignmentForType(typeName string) uint32 {
	if strings.HasPrefix(typeName, "STRING(") {
		return 1
	}
	switch typeName {
	case "WORD", "UINT", "INT":
		return 2
	case "DWORD", "UDINT", "DINT", "REAL", "TIME", "TOD", "DATE", "DT":
		return 4
	case "LREAL":
		return 8
	default:
		// BOOL, BYTE, USINT, SINT and unrecognised types: 1-byte alignment.
		return 1
	}
}

// configNode is an internal tree node produced by parseBlockTree.
type configNode interface {
	// maxAlignment returns the natural alignment of this node
	// (leaf: type alignment; container: max of children's alignments).
	maxAlignment() uint32
	// emitActions appends the corresponding ConfigActions to the slice.
	// parentAlignment is the effective alignment of the enclosing container;
	// it is propagated into all-byte-aligned (own alignment == 1) child
	// containers so their trailing padding matches TwinCAT's pack_mode=3
	// behavior.
	emitActions(actions *[]ConfigAction, parentAlignment uint32)
	// emitPins appends the leaf ConfigPins to the slice.
	emitPins(pins *[]ConfigPin)
}

// configLeafNode represents a single leaf symbol in the config tree.
type configLeafNode struct {
	pin       ConfigPin
	alignment uint32
}

func (n *configLeafNode) maxAlignment() uint32 { return n.alignment }

func (n *configLeafNode) emitActions(actions *[]ConfigAction, _ uint32) {
	*actions = append(*actions, ConfigAction{
		Kind:      ConfigActionPin,
		Pin:       n.pin,
		Alignment: n.alignment,
	})
}

func (n *configLeafNode) emitPins(pins *[]ConfigPin) {
	*pins = append(*pins, n.pin)
}

// configContainerNode represents a struct or array-element container.
type configContainerNode struct {
	adsName  string // full ADS symbol name for this container, e.g. "DISPLAY_DATA.stData.aPools[1]" for array elements or "DISPLAY_DATA.stData" for structs
	children []configNode
}

func (n *configContainerNode) maxAlignment() uint32 {
	var max uint32 = 1
	for _, child := range n.children {
		if a := child.maxAlignment(); a > max {
			max = a
		}
	}
	return max
}

func (n *configContainerNode) emitActions(actions *[]ConfigAction, parentAlignment uint32) {
	ownAlignment := n.maxAlignment()
	// TwinCAT pack_mode=3 (default 8-byte pack boundary): when a struct has
	// only byte-aligned members (own alignment == 1) it inherits the enclosing
	// container's effective alignment for trailing padding.  Structs with own
	// alignment > 1 use their own alignment (e.g. a struct containing WORD or
	// REAL is padded to a multiple of 2 or 4 respectively).
	effectiveAlignment := ownAlignment
	if ownAlignment == 1 && parentAlignment > 1 {
		effectiveAlignment = parentAlignment
	}
	*actions = append(*actions, ConfigAction{Kind: ConfigActionBeginContainer, Alignment: ownAlignment, ADSName: n.adsName})
	for _, child := range n.children {
		child.emitActions(actions, effectiveAlignment)
	}
	*actions = append(*actions, ConfigAction{Kind: ConfigActionEndContainer, Alignment: effectiveAlignment, ADSName: n.adsName})
}

func (n *configContainerNode) emitPins(pins *[]ConfigPin) {
	for _, child := range n.children {
		child.emitPins(pins)
	}
}

// configLine is a pre-processed line from the config file.
type configLine struct {
	lineNo  int
	depth   int    // indent depth (1 unit = 2 spaces)
	trimmed string // content without leading whitespace
}

// pathFrame tracks one level of the nesting hierarchy during parsing.
type pathFrame struct {
	// halSeg is the path segment used in HAL pin names (e.g. "stPOOL" or "stPOOL.1").
	halSeg string
	// adsSeg is the path segment used in ADS symbol names (e.g. "stPOOL" or "stPOOL[1]").
	adsSeg string
	// depth is the indent depth at which this frame was pushed.
	depth int
}

// ParseConfig reads the HAL-ADS config format from r and returns the list of
// leaf symbols to create as HAL pins.
//
// Format (2-space indentation):
//
//	ContainerName
//	  in leafName TYPE
//	  out leafName TYPE
//	  ArrayName[start..end]
//	    in leafName TYPE
func ParseConfig(r io.Reader) ([]ConfigPin, error) {
	actions, err := ParseConfigActions(r)
	if err != nil {
		return nil, err
	}
	var pins []ConfigPin
	for _, a := range actions {
		if a.Kind == ConfigActionPin {
			pins = append(pins, a.Pin)
		}
	}
	return pins, nil
}

// ParseConfigActions parses the HAL-ADS config format and returns an ordered
// sequence of ConfigActions that describes the symbol tree with alignment
// information.  Container boundaries are marked with ConfigActionBeginContainer
// and ConfigActionEndContainer, each carrying the container's natural alignment
// (the maximum natural alignment of all its members, computed recursively).
func ParseConfigActions(r io.Reader) ([]ConfigAction, error) {
	lines, err := readConfigLines(r)
	if err != nil {
		return nil, err
	}
	stack := []pathFrame{{halSeg: "", adsSeg: "", depth: -1}}
	idx := 0
	nodes, err := parseBlockTree(lines, &idx, -1, stack)
	if err != nil {
		return nil, err
	}
	var actions []ConfigAction
	for _, node := range nodes {
		node.emitActions(&actions, 1)
	}
	return actions, nil
}

// readConfigLines reads and pre-processes all non-blank, non-comment lines.
func readConfigLines(r io.Reader) ([]configLine, error) {
	scanner := bufio.NewScanner(r)
	var lines []configLine
	lineNo := 0
	for scanner.Scan() {
		lineNo++
		rawLine := scanner.Text()
		trimmed := strings.TrimSpace(rawLine)
		if trimmed == "" || strings.HasPrefix(trimmed, "#") {
			continue
		}
		// Calculate indent depth: count leading 2-space pairs.
		depth := 0
		for depth*2+2 <= len(rawLine) && strings.HasPrefix(rawLine[depth*2:], "  ") {
			depth++
		}
		lines = append(lines, configLine{lineNo: lineNo, depth: depth, trimmed: trimmed})
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("config read error: %w", err)
	}
	return lines, nil
}

// parseBlockTree processes config lines starting at *idx, building an internal
// tree of configNodes.  It returns all top-level nodes in this block.
//
//   - minDepth: lines with depth <= minDepth are left to the parent caller.
//   - stack: current path-frame stack used for building leaf symbol paths.
//
// For each container (struct or array), sub-lines (those at depth > container
// depth) are collected and processed recursively, so that alignment can be
// computed bottom-up before emitting BeginContainer actions.
func parseBlockTree(lines []configLine, idx *int, minDepth int, stack []pathFrame) ([]configNode, error) {
	var nodes []configNode

	for *idx < len(lines) {
		cl := lines[*idx]
		if cl.depth <= minDepth {
			return nodes, nil
		}

		tokens := strings.Fields(cl.trimmed)
		if len(tokens) == 0 {
			*idx++
			continue
		}

		// Leaf line (starts with "in" or "out").
		if tokens[0] == "in" || tokens[0] == "out" {
			if len(tokens) < 3 {
				return nil, fmt.Errorf("line %d: leaf line requires direction, name, and type", cl.lineNo)
			}
			dir := PinDir(tokens[0])
			name := tokens[1]
			typeName := parseTypeName(tokens[2:])
			halPath := buildPath(stack, name, false)
			adsName := buildPath(stack, name, true)
			node := &configLeafNode{
				pin: ConfigPin{
					Dir:      dir,
					HALPath:  halPath,
					ADSName:  adsName,
					TypeName: typeName,
				},
				alignment: alignmentForType(typeName),
			}
			nodes = append(nodes, node)
			*idx++
			continue
		}

		// Container line: plain struct or array.
		expanded, err := expandContainer(tokens[0])
		if err != nil {
			return nil, fmt.Errorf("line %d: %w", cl.lineNo, err)
		}

		*idx++ // consume container line

		// Collect sub-block: all lines at depth > cl.depth.
		subStart := *idx
		for *idx < len(lines) && lines[*idx].depth > cl.depth {
			*idx++
		}
		subLines := lines[subStart:*idx]

		for _, inst := range expanded {
			newStack := make([]pathFrame, len(stack))
			copy(newStack, stack)
			newStack = append(newStack, pathFrame{
				halSeg: inst.halSeg,
				adsSeg: inst.adsSeg,
				depth:  cl.depth,
			})
			containerADSName := buildPath(stack, inst.adsSeg, true)
			subIdx := 0
			children, err := parseBlockTree(subLines, &subIdx, cl.depth-1, newStack)
			if err != nil {
				return nil, err
			}
			nodes = append(nodes, &configContainerNode{adsName: containerADSName, children: children})
		}
	}

	return nodes, nil
}

// containerInstance represents one element of a parsed container path.
type containerInstance struct {
	halSeg string
	adsSeg string
}

// expandContainer parses a container token.
// For "name" it returns [{halSeg:"name", adsSeg:"name"}].
// For "name[1..9]" it returns nine instances.
func expandContainer(token string) ([]containerInstance, error) {
	// Detect array syntax: name[start..end]
	lb := strings.Index(token, "[")
	if lb == -1 {
		return []containerInstance{{halSeg: token, adsSeg: token}}, nil
	}
	rb := strings.Index(token, "]")
	if rb == -1 || rb < lb {
		return nil, fmt.Errorf("invalid array syntax %q", token)
	}
	baseName := token[:lb]
	rangeStr := token[lb+1 : rb]
	parts := strings.SplitN(rangeStr, "..", 2)
	if len(parts) != 2 {
		return nil, fmt.Errorf("invalid array range %q (expected start..end)", rangeStr)
	}
	var start, end int
	if _, err := fmt.Sscanf(parts[0], "%d", &start); err != nil {
		return nil, fmt.Errorf("invalid array start %q", parts[0])
	}
	if _, err := fmt.Sscanf(parts[1], "%d", &end); err != nil {
		return nil, fmt.Errorf("invalid array end %q", parts[1])
	}
	if start > end {
		return nil, fmt.Errorf("array range start %d > end %d", start, end)
	}

	var instances []containerInstance
	for i := start; i <= end; i++ {
		instances = append(instances, containerInstance{
			halSeg: fmt.Sprintf("%s.%d", baseName, i),
			adsSeg: fmt.Sprintf("%s[%d]", baseName, i),
		})
	}
	return instances, nil
}

// buildPath constructs a dot-separated path from the current stack + leaf name.
// If adsNotation is false, HAL dot notation is used (halSeg).
// If adsNotation is true, ADS bracket notation is used (adsSeg).
func buildPath(stack []pathFrame, leafName string, adsNotation bool) string {
	var parts []string
	for _, f := range stack {
		if f.halSeg == "" {
			continue
		}
		if adsNotation {
			parts = append(parts, f.adsSeg)
		} else {
			parts = append(parts, f.halSeg)
		}
	}
	parts = append(parts, leafName)
	return strings.Join(parts, ".")
}

// parseTypeName reconstructs the type name from the remaining tokens on a leaf line.
// Handles "bool", "dint", "string(32)" etc. Returns the normalised upper-case name.
func parseTypeName(tokens []string) string {
	if len(tokens) == 0 {
		return ""
	}
	// Normalize to upper case for ADS type names.
	return strings.ToUpper(strings.Join(tokens, ""))
}
