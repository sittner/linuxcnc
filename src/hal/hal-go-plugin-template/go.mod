module linuxcnc.org/hal-go-plugin-template

go 1.21

require (
	github.com/sittner/linuxcnc/src/launcher v0.0.0
	linuxcnc.org/hal v0.0.0
)

replace (
	github.com/sittner/linuxcnc/src/launcher => ../../launcher
	linuxcnc.org/hal => ../hal-go
)
