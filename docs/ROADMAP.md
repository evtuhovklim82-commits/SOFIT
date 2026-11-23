# Roadmap
1. Add undo/redo, unsaved-changes warning, zoom and snap/grid.
2. Separate geometry primitives and document command layer; add unit tests.
3. Add measurement tables and parametric dependency graph.
4. Add pattern pieces, seam allowances, grading, DXF/PDF export.
5. Research legal interoperability with external formats independently.

## Parametric modeling after 2.1

SOFIT 2.1 keeps named seam chains, their sewing direction, target ease, linked notch pairs and optional common allowances in the project. These relationships are checked against the base and each graded size.

The next modeling milestone is a general operation graph with independent operation and output identities, editable input references, and a migration strategy for downstream dependencies. Curved cuts, curved princess seams and more general dart transfers need topology and grading tests before entering the template library. Reusable user-defined formula blocks also need independent names and explicit input and output bindings.
