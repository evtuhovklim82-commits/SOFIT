# SOFIT project format

SOFIT 4.0 opens older SOFIT projects and writes schema 14 with default production and product options. Product information and piece clearance use schema 15. A configured repeat or customized tech pack uses schema 16. Named seam repeat points, fabric flaws and finished pattern measurements use schema 17. A project contains the editable pattern and its production data. DXF carries exchange geometry and PDF carries print output. GRAFIS MDL support requires a separate analysis of actual MDL files.

## Schemas 14–18

The file is a UTF-8 JSON object with these fields.

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | `SOFIT` |
| `version` | integer | `14`, `15`, `16`, `17` or `18` |
| `payload` | string | A serialized UTF-8 JSON project object, escaped as a JSON string |
| `sha256` | string | Lowercase hexadecimal SHA-256 of the UTF-8 bytes of the decoded payload string |

Validate the digest before parsing the payload. Hash the decoded string verbatim, including any whitespace, without reserializing its JSON object. The digest detects accidental corruption. It does not authenticate the author. Readers limit the outer file to 64 MiB. Saving validates the serialized payload by replaying it through the same reader, then uses an atomic replacement in the destination directory. Direct-write fallback is disabled.

The payload reuses the SOFIT 7–13 records and includes an explicit boolean `useDraftMeasurements`. Schema 15 uses the same envelope. Its optional `productInformation` object has string fields `styleCode`, `title`, `revision`, `material` and `notes`. The optional `production.pieceClearanceMm` is a number between 0 and 1000. Omitted fields give empty product information and zero clearance. SOFIT 3.0 reads schema 14. SOFIT versions before 3.0 require the older project structure.

Schema 16 adds the optional `production.fabricRepeatMm` array `[across, along]`. Each value is zero (disabled) or 0.001–10,000 mm. It also adds an optional `techPack` object with the strings `reportTitle`, `billOfMaterials`, `assemblyInstructions` and the booleans `includeCuttingQuantities` and `includeSizeMeasurements`. By default the quantities are included and the draft measurements are omitted.

Schema 17 adds optional `production.fabricFlaws` rectangles as arrays `[x, y, width, length]` in millimetres from the fabric origin. Up to 100 rectangles are supported. Coordinates are non-negative, both dimensions are at least 0.001 mm, and every rectangle ends within 1,000,000 mm. A placement can store the two fields `repeatEdge` (a named main-layer line or quadratic curve on its piece) and `repeatParameter` (0–1 in edge parameter order). The matching point is evaluated on the graded seam, then transformed using the placement's grain, rotation and hand. Omitting those fields uses the oriented cutting outline's top-left extent. For curves the parameter is a Bézier parameter, independent of the arc-length fractions used for notches. The optional `finishedMeasurements` array stores up to 100 rows with a unique `label` and distinct named draft points `first` and `second`. The PDF computes their straight-line distance by size. A missing point produces an unavailable entry.

Schema 18 adds two optional records. `production.fabricShrinkagePercent` is `[across width, along length]` in percent, from 0 through 99 per axis. The cutting transform applies `100 / (100 − percentage)` to each axis after aligning the grain to the fabric. Fabric dimensions, placements, clearance, defects and repeat lengths refer to unprocessed fabric in millimetres. Changing compensation can require positioning pieces again. The finished draft remains in its original dimensions.

A spread, pleat or gather history step can store a `followingGuides` array of names. Its presence enables transport of the listed guides and earlier fold marks when the moving panel shifts. An empty array can be used on the final slash to preserve its earlier fold marks. Steps without the field retain their previous replay rules. Transport requires the guide to stay on one side of the active cut.

| Payload record | Contents |
| --- | --- |
| `objects` | Ordered points, lines, rectangles, circles and quadratic curves. Coordinates are in millimetres, with the quadratic control point following the endpoints. The piece index is zero-based or `-1` for an unassigned object. Objects on the main and construction layers can carry a DXF source-layer name and a draft binding. |
| `pieces` | Ordered piece names, cut count, size label, label visibility, allowance width in millimetres, optional grain angle in degrees and piece-wide notch positions as percentages. |
| `layerVisibility` | Three booleans for the main, construction and computed allowance layers, in that order. |
| `production` | Named sizes, size measurements, piece and coordinate-based grading offsets, fabric width, directional fabric and half-turn options. Placements reference a piece and size and retain location, rotation, hand, fold and fixed orientation. |
| `parametricDraft` | Base measurements, parameter and point formulas, and edges referencing named points, with an optional control-point reference. |
| `modelingHistory`, `modelingSources` | Ordered parametric operations and the source bindings needed to replay contour changes. |
| Edge and seam metadata | Optional `edgeAllowanceMm` and `edgeNotches` on lines and curves. Edge notches are fractions of arc length. `seamLinks` retain paired edge chains, traversal direction, ease, tolerance, allowances and paired-notch counts. |

New sleeve drafts can use `capcontrol(width, rise, target)` in point formulas. The function returns the horizontal distance from the sleeve crown to the Bézier control in millimetres. It keeps the specified cap rise and bicep width and limits the control to 20–95% of the half-width. The draft measurement `sleeve_cap_ease` supplies the target ease per section and can be overridden by size. Existing formulas retain their original meaning. Older SOFIT builds may reject formulas that call this function.

Projects that record dart redistribution use the modeling operation keys `split-dart` and `merge-darts`. Each source dart is represented by two named construction legs, followed by two outlet guides for a split or one outlet guide for a merge. For `split-dart`, the history field `width` records the first outlet's angular share as a percentage strictly between 0 and 100. Earlier readers that lack these operation keys cannot replay such records.

Payload versions 7–9 describe the base project records. Versions 10–12 identify edge metadata, modeling history and contour topology, with `baseVersion` identifying the base records. Version 13 adds seam links and uses `linkedBaseVersion` to identify the intermediate modeling or edge records. The outer version governs compatibility for the complete file.

Formula geometry and modeling results are recomputed at open using the saved definitions. Stored objects preserve editable free geometry, ordering and bindings. Allowance contours, linked seam diagnostics and graded outlines are computed from the saved data. Undo history, in-progress gestures, comparison size overlays and selections are transient. Application preferences, including the panel arrangement and keyboard shortcuts, belong to the local workspace.

## Migration

Open legacy SOFIT schemas 1–13 or the original OpenPatternCAD schema 1 with **File → Open**. Use **Save As** to keep the source file and create a schema 14 copy. Legacy version 8 with size rows retains reference-only measurements and manual offsets. Legacy contour edges with numeric seam suffixes are mapped to their canonical names. The source file is unchanged by opening it. Future schema versions and damaged payloads show an error and leave the active drawing intact.

The `examples` directory includes the legacy apron, skirt and trousers, plus schema 14 copies of the skirt and trousers. Use the skirt to edit the silhouette and compare sizes. Use the trousers to inspect linked seams and size measurements.

## Saving and recovery

- **Save** replaces the current `.sofit` through an atomic write. **Save As** picks a new destination and confirms an overwrite. Use **Save As** to preserve a legacy copy before editing.
- SOFIT checks for edits every 30 seconds and writes a separate recovery snapshot after changes. Each active editor owns its snapshot. The original project is untouched by autosave. If a snapshot update fails, the last complete snapshot remains available.
- On startup, or from **File → Recover Unsaved Project**, select a snapshot by project name and timestamp. It opens as an unsaved drawing. Review it and use **Save As** to keep it. A successful save or an explicit discard retires its snapshot.
- Changes since the last successful snapshot may be lost after a crash. Recovery data is stored in the `recovery` subdirectory of Qt's per-user application data location. On typical Windows installs that is `%APPDATA%\SOFIT Community\SOFIT\recovery`. On Linux it is typically `~/.local/share/SOFIT Community/SOFIT/recovery`. A damaged snapshot is retained for inspection. Close SOFIT before moving or removing recovery files by hand.

On import, review DXF units, selected layers, approximations and skipped entities. On export, select the intended size and retain the SOFIT project for further editing. For PDF printing, use actual size and verify the calibration mark.


### Повторное применение последовательности операций

Источником служит обычный проект `.sofit`, сохранённый с историей построения.
В совместимом проекте выберите «Применить операции из проекта…» в библиотеке конструкций.
Операции ищут исходные объекты, оси и направляющие по постоянным именам.
Если в текущем проекте уже есть операции, его история должна совпадать с началом исходной.
Импорт продолжается с первой отсутствующей операции и проверяет топологию и пересчёт каждого размера.
Операции используют мерки и детали текущего проекта. При ошибке текущий проект остаётся без изменений.
Применение последовательности можно отменить или повторить одной командой.

### Распределение вытачек и размерно-ростовая таблица

- Для разделения выберите две ножки и два вывода на слое построения. Для объединения выберите две пары ножек и один вывод. Все направляющие исходят из одной точки внутри детали и заканчиваются на контуре. У исходных вытачек должны быть равные ножки и прямые линии раствора.
- При разделении укажите долю углового раствора для первого вывода. Вершины разделённых вытачек отступают от общей точки на 20% расстояния до середины соответствующего раствора, чтобы получать две отдельные вершины с равными ножками. Если существующий припуск пересекает новый контур, команда отклоняет изменение.
- В редакторе градации команда «Добавить по размеру и росту…» использует четыре опорные строки. Сначала рассчитываются значения в обоих рядах роста по доле размера, затем между рядами по доле роста. Можно включить расчёт смещений для совпадающих точек. Полученные мерки и смещения сохраняются как обычные редактируемые строки.

A manually placed drill is stored as a piece-assigned construction point using the existing `dxfLayer` value `SOFIT_DRILL`. Pattern DXF retains the point and its layer. Marker DXF writes the placed center as a point on the same layer. Marker PDF draws a circle of 1 mm diameter at the placed center. The project format version is unchanged.

A `spread`, `pleat` or `gather` modeling step can store `width` as a formula string in millimetres. A JSON number retains its earlier meaning. The formula uses the draft's measurements, parameters and point coordinates and is checked in the base and every measurement-graded size. The ordered list `guides` can contain up to 32 end-to-end named straight or quadratic segments for one cut. Earlier builds may reject projects using either extension.
