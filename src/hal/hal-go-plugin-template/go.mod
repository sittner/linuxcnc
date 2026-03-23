module linuxcnc.org/hal-go-plugin-template

go 1.21

require (
	linuxcnc.org/hal v0.0.0
	github.com/sittner/linuxcnc/src/launcher v0.0.0
)

replace (
	linuxcnc.org/hal => ../hal-go
	github.com/sittner/linuxcnc/src/launcher => ../../launcher
)
