# TP (Trajectory Planner) Isolation Project

## Purpose

This directory contains comprehensive documentation for isolating LinuxCNC's trajectory planner (TP) code into a standalone, testable library. The goal is to decouple the TP from the motion controller module, making it:

- **More testable**: Unit tests without requiring full RTAPI/HAL infrastructure
- **More reusable**: Usable in other projects or external modules
- **Better architected**: Clear interfaces and separation of concerns
- **Easier to maintain**: Reduced coupling and improved modularity
- **More portable**: Fewer platform-specific dependencies

## Current Status: Planning Phase

This is currently a **documentation and planning effort**. No code changes have been made yet. The documents here represent a comprehensive roadmap for incremental refactoring that can be executed in multiple smaller PRs.

## Documentation Structure

```
/isolated-tp/
├── README.md                               # This file - project overview
├── MIGRATION_PLAN.md                       # Complete migration strategy with phases
├── DEPENDENCY_ANALYSIS.md                  # Detailed dependency analysis
├── API_DESIGN.md                           # Proposed library API design
├── BENEFITS.md                             # Benefits and justification
├── COMPATIBILITY.md                        # Compatibility and migration strategy
└── docs/
    ├── phase1-abstraction-layer.md         # Phase 1 implementation details
    ├── phase2-refactoring.md               # Phase 2 implementation details
    ├── phase3-library-extraction.md        # Phase 3 implementation details
    └── phase4-testing.md                   # Phase 4 implementation details
```

## Key Documents

### [MIGRATION_PLAN.md](MIGRATION_PLAN.md)
The master plan document describing the overall strategy, timeline, phases, risks, and success criteria.

### [DEPENDENCY_ANALYSIS.md](DEPENDENCY_ANALYSIS.md)
Detailed analysis of all current dependencies, categorized by complexity and impact. Essential reading before starting any refactoring work.

### [API_DESIGN.md](API_DESIGN.md)
Proposed public API for the isolated TP library, including structures, functions, and migration compatibility layer.

### Phase Documentation
Each phase has a detailed document in the `docs/` directory with specific tasks, code examples, testing approaches, and completion checklists.

## How to Contribute

### For Reviewers
1. Start with [MIGRATION_PLAN.md](MIGRATION_PLAN.md) for the big picture
2. Review [DEPENDENCY_ANALYSIS.md](DEPENDENCY_ANALYSIS.md) to understand current coupling
3. Evaluate [API_DESIGN.md](API_DESIGN.md) for the proposed interface
4. Provide feedback on approach, timeline, and priorities

### For Implementers
1. Read all documentation in this directory
2. Pick a phase to work on (preferably in order: Phase 1 → 2 → 3 → 4)
3. Create a PR targeting a specific subset of tasks from that phase
4. Keep PRs small and focused (e.g., "Create tp_platform.h abstraction")
5. Ensure full backward compatibility
6. Add tests for your changes

### For Users/Integrators
See [COMPATIBILITY.md](COMPATIBILITY.md) to understand how existing code will continue to work during and after the migration.

## Principles

This migration follows these core principles:

1. **Incremental**: Can be done in small, reviewable PRs
2. **Non-breaking**: Maintains full backward compatibility throughout
3. **Testable**: Each phase includes verification steps
4. **Reversible**: Changes can be rolled back if issues arise
5. **Documented**: Each change is well-documented with rationale

## Timeline

Estimated total effort: **6-10 weeks** of focused development time

- Phase 1 (Abstraction Layer): 1-2 weeks
- Phase 2 (Refactoring): 2-3 weeks
- Phase 3 (Library Extraction): 1-2 weeks
- Phase 4 (Unit Testing): 2-3 weeks

These are estimates for a single developer working on this full-time. Actual timeline will depend on review cycles, testing, and coordination with the broader LinuxCNC project.

## Success Criteria

The migration will be considered successful when:

1. TP code compiles as a standalone library with minimal dependencies
2. Library can be used without RTAPI/HAL infrastructure
3. Unit tests achieve target coverage (60%+ for core TP code)
4. All existing LinuxCNC integration tests pass
5. No measurable performance regression
6. External modules (like tpcomp.comp) continue to work
7. Documentation is complete and maintained

## Related Issues and Discussions

*(Add links to relevant GitHub issues, mailing list discussions, or forum threads here)*

- Issue: [To be created] - TP Isolation Tracking Issue
- Discussion: [Link to mailing list thread if applicable]

## References

### Current TP Code Location
- Main TP source: `src/emc/tp/`
- Core files: tp.c, tc.c, tcq.c, blendmath.c, spherical_arc.c
- Headers: tp.h, tc.h, tcq.h, blendmath.h, spherical_arc.h, tp_types.h, tc_types.h
- Tests: `unit_tests/tp/` (currently minimal)

### Dependencies
- Posemath library: `src/libnml/posemath/` (already modular)
- Motion module: `src/emc/motion/`
- RTAPI: `src/rtapi/`

### External Resources
- LinuxCNC Developer Documentation: [link]
- Trajectory Planning Overview: [link to docs]
- RTAPI Documentation: [link]

## Questions?

For questions about this migration plan:
1. Check the documentation in this directory
2. Review the dependency analysis for specific technical questions
3. Post on the LinuxCNC developer mailing list
4. Open a GitHub issue with the `trajectory-planner` label

---

**Note**: This is a living document that will be updated as the migration progresses. All documentation should be kept current with actual implementation status.
