# Axis Multi-Client Architecture

## Goal

Centralize all UI state in the server-side companion (currently `axis_ui` cmod)
so that multiple axis client instances can run simultaneously without conflicts.

## Current Problems

- Each axis client holds its own state (loaded file, slider values, jog increment, mode)
- Starting a second client re-issues mode switches and file opens, conflicting with running programs
- HAL pins are "last writer wins" — no defined owner in multi-client setups
- Startup during program execution causes rejected NML commands and slow window appearance

## Architecture

```
┌──────────────────────┐     ┌─────────────────────────────────┐
│  axis client (thin)  │     │  axis_ui server component       │
│                      │◄───►│  (cmod or gomod in gomc-server) │
│  - renders UI        │ WS  │                                 │
│  - sends user intent │     │  Owns:                          │
│  - syncs display     │     │  - loaded_file                  │
│  - zero NML calls    │     │  - jog_increment / jog_axis     │
│                      │     │  - feed_override                │
└──────────────────────┘     │  - spindle_override             │
                             │  - max_velocity                 │
┌──────────────────────┐     │  - coordinate_type              │
│  axis client #2      │◄───►│  - HAL pins                     │
└──────────────────────┘     │  - file open logic              │
                             │  - mode management              │
                             │                                 │
                             │  Lives in same process as       │
                             │  task/motion (gomc-server)      │
                             │  → no IPC, direct function calls│
                             └─────────────────────────────────┘
```

## Key Principles

1. **Server owns state** — clients are stateless views that push user intent
2. **State sync via watch** — all clients subscribe and get pushed updates when state changes (another client changes jog increment → all update)
3. **Command serialization** — server decides mode switches, queues if interpreter busy
4. **File management centralized** — `open_file(path)` is an API call; server handles mode switch + plan_synch + program_open atomically
5. **HAL pins reflect server state** — not "last client to set wins"

## Migration Order (incremental)

1. **Sliders** (feed override, spindle override, max velocity)
   - Already have HAL pins
   - Server owns values, pushes to clients
   - Client sends `set_feed_override(value)` intent

2. **Jog settings** (axis, increment)
   - Already partially in axisui API
   - Server validates and applies

3. **File management** (open, reload, close)
   - Biggest piece, highest payoff
   - Server handles ensure_mode + interpreter commands
   - Clients get notified of success + file content

4. **Mode management**
   - Last — requires most careful coordination
   - Server becomes sole issuer of mode changes
   - Clients request intent ("I want to run"), server orchestrates

## Implementation Options

The server-side component can be implemented as either:

- **cmod** (current `axis_ui`): C module loaded by gomc-server, exposes HAL pins directly
- **gomod**: Pure Go module within gomc-server — may be preferable if the state logic becomes complex enough that Go's ergonomics help, and HAL pin access can be done via the Go HAL bindings

Choose based on whether HAL pin manipulation or state logic dominates the complexity.

## What This Solves

| Problem | Solution |
|---------|----------|
| Startup conflicts during execution | Server knows state; client just syncs |
| Mode switch races between clients | Single command issuer |
| Slider/pin inconsistency | Server is authority |
| Duplicate file opens | Server tracks loaded file; new client reads it |
| NML replacement path | Server uses GMI internally; NML eliminated |

## Notes

- gomc-server unifies cmod + task + motion in one process — "moving logic to server" means no IPC overhead
- NML will be replaced by GMI calls (one of the goals of the gomc project)
- The axisui `.gmi` IDL already defines part of this interface — expand it incrementally
- Existing axis client code can be thinned step by step (remove ensure_mode, remove direct NML calls, replace with API calls)
