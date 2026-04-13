package parser

import (
	"fmt"
	"strconv"

	"github.com/sittner/linuxcnc/src/launcher/internal/gmicompile/ast"
)

// Parser parses GMI source into an AST.
type Parser struct {
	scanner *Scanner
	cur     Token
	file    string
	errors  []string
}

// Parse parses a GMI file and returns the AST and any errors.
func Parse(filename, src string) (*ast.API, []string) {
	p := &Parser{
		scanner: NewScanner(src),
		file:    filename,
	}
	p.advance()
	api := p.parseAPI()
	return api, p.errors
}

func (p *Parser) advance() {
	p.cur = p.scanner.Scan()
}

func (p *Parser) pos() ast.Pos {
	return ast.Pos{File: p.file, Line: p.cur.Line, Col: p.cur.Col}
}

func (p *Parser) errorf(format string, args ...interface{}) {
	msg := fmt.Sprintf("%s:%d:%d: %s", p.file, p.cur.Line, p.cur.Col, fmt.Sprintf(format, args...))
	p.errors = append(p.errors, msg)
}

func (p *Parser) expect(t TokenType) bool {
	if p.cur.Type != t {
		p.errorf("expected %v, got %q", t, p.cur.Text)
		return false
	}
	p.advance()
	return true
}

func (p *Parser) parseAPI() *ast.API {
	api := &ast.API{}

	for p.cur.Type != EOF {
		switch {
		case p.cur.Type == AT:
			p.parseDirective(api)
		case p.cur.Type == ENUM:
			api.Enums = append(api.Enums, p.parseEnum())
		case p.cur.Type == TYPE:
			api.Types = append(api.Types, p.parseType())
		case p.cur.Type == FUNC:
			api.Funcs = append(api.Funcs, p.parseFunc())
		default:
			p.errorf("unexpected token %q", p.cur.Text)
			p.advance()
		}
	}
	return api
}

func (p *Parser) parseDirective(api *ast.API) {
	p.advance() // skip @
	name := p.cur.Text
	p.advance()

	switch name {
	case "api":
		api.Name = p.cur.Text
		api.Pos = p.pos()
		p.advance()
	case "version":
		if v, err := strconv.Atoi(p.cur.Text); err == nil {
			api.Version = v
		}
		p.advance()
	case "prefix":
		api.Prefix = p.cur.Text
		p.advance()
	case "rest_export":
		api.RestExport = p.cur.Text == "true"
		p.advance()
	}
}

func (p *Parser) parseEnum() ast.Enum {
	pos := p.pos()
	p.advance() // skip "enum"
	name := p.cur.Text
	p.advance()

	enum := ast.Enum{Name: name, Pos: pos}
	p.expect(LBRACE)

	for p.cur.Type != RBRACE && p.cur.Type != EOF {
		vpos := p.pos()
		vname := p.cur.Text
		p.advance()
		p.expect(EQ)
		val, _ := strconv.Atoi(p.cur.Text)
		p.advance()
		enum.Values = append(enum.Values, ast.EnumValue{Name: vname, Value: val, Pos: vpos})
	}
	p.expect(RBRACE)
	return enum
}

func (p *Parser) parseType() ast.Type {
	pos := p.pos()
	p.advance() // skip "type"
	name := p.cur.Text
	p.advance()

	typ := ast.Type{Name: name, Pos: pos}
	p.expect(LBRACE)

	for p.cur.Type != RBRACE && p.cur.Type != EOF {
		fpos := p.pos()
		fname := p.cur.Text
		p.advance()
		p.expect(COLON)
		ftype := p.parseTypeRef()
		typ.Fields = append(typ.Fields, ast.Field{Name: fname, Type: ftype, Pos: fpos})
	}
	p.expect(RBRACE)
	return typ
}

func (p *Parser) parseFunc() ast.Func {
	pos := p.pos()
	p.advance() // skip "func"
	name := p.cur.Text
	p.advance()

	fn := ast.Func{Name: name, Pos: pos}

	// Parameters
	p.expect(LPAREN)
	for p.cur.Type != RPAREN && p.cur.Type != EOF {
		ppos := p.pos()
		pname := p.cur.Text
		p.advance()
		p.expect(COLON)
		ptype := p.parseTypeRef()
		fn.Params = append(fn.Params, ast.Param{Name: pname, Type: ptype, Pos: ppos})
		if p.cur.Type == COMMA {
			p.advance()
		}
	}
	p.expect(RPAREN)

	// Return type
	if p.cur.Type == ARROW {
		p.advance()
		ret := p.parseTypeRef()
		fn.Return = &ret
	}

	// Body with annotations
	p.expect(LBRACE)
	for p.cur.Type != RBRACE && p.cur.Type != EOF {
		if p.cur.Type == AT {
			p.advance()
			aname := p.cur.Text
			p.advance()
			aval := p.cur.Text
			p.advance()
			switch aname {
			case "method":
				fn.Method = aval
			case "path":
				fn.Path = aval
			case "rt_safe":
				fn.RTSafe = aval == "true"
			case "doc":
				fn.Doc = aval
			}
		} else {
			p.advance()
		}
	}
	p.expect(RBRACE)
	return fn
}

func (p *Parser) parseTypeRef() ast.TypeRef {
	// []T slice
	if p.cur.Type == LBRACKET {
		p.advance()
		if p.cur.Type == RBRACKET {
			p.advance()
			elem := p.parseTypeRef()
			return ast.TypeRef{Kind: ast.TypeSlice, Elem: &elem}
		}
		// [T; N] array
		elem := p.parseTypeRef()
		p.expect(SEMI)
		size, _ := strconv.Atoi(p.cur.Text)
		p.advance()
		p.expect(RBRACKET)
		return ast.TypeRef{Kind: ast.TypeArray, Elem: &elem, ArrayLen: size}
	}

	name := p.cur.Text
	p.advance()

	nullable := false
	if p.cur.Type == QUESTION {
		nullable = true
		p.advance()
	}

	kind := ast.TypeNamed
	if ast.Primitives[name] {
		kind = ast.TypePrimitive
	}
	return ast.TypeRef{Kind: kind, Name: name, Nullable: nullable}
}
