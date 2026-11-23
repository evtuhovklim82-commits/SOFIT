# Roadmap
1. Add undo/redo, unsaved-changes warning, zoom and snap/grid.
2. Separate geometry primitives and document command layer; add unit tests.
3. Add measurement tables and parametric dependency graph.
4. Add pattern pieces, seam allowances, grading, DXF/PDF export.
5. Research legal interoperability with external formats independently.

## GRAFIS 26 ⇄ SOFIT workflow

SOFIT 2.2 provides stage menus, a workflow toolbar, user-defined shortcuts, and a reference for documented tasks.
Review both directions with a working GRAFIS 26 pattern maker using the same brief. Start from a basic
block, spread a piece, recalculate another size, prepare seam allowances, and place copies on fabric.
Record exact paths and keystrokes, first-attempt success, backtracking, and avoidable clicks.
Compare results by size before treating the operations as interchangeable. GRAFIS shortcut presets
and exact command sequences need first-hand verification with the operator.

## Deferred parametric modeling

SOFIT 2.1 keeps named seam chains, their sewing direction, target ease, linked notch pairs and optional common allowances in the project. These relationships are checked against the base and each graded size.

The next modeling milestone is a general operation graph with independent operation and output identities, editable input references, and a migration strategy for downstream dependencies. Curved cuts, curved princess seams and more general dart transfers need topology and grading tests before entering the template library. Reusable user-defined formula blocks also need independent names and explicit input and output bindings.
