# SOFIT 4.0 Professional and GRAFIS 26

The SOFIT 3.0 comparison baseline is `5901543`. The second SOFIT 4.0 update builds on `ee88ae1`. This cycle starts from `0043f5a` and expands the comparison below. The comparison uses GRAFIS vendor documentation and the SOFIT source and tests. The GRAFIS application and production hardware were outside the verification setup. The table describes coverage by workflow.

| Workflow | GRAFIS documentation | SOFIT 3.0 implementation | SOFIT 4.0 and remaining gaps |
| --- | --- | --- | --- |
| Interactive constructions and library | [Interactive interface](https://help.grafis.de/en-gb/Content/Interaktive_Konstruktionen-Basiswissen/Interaktive_Oberflaeche.htm), [library configuration](https://help.grafis.de/en-gb/Content/Module/Die_Gestaltung_der_Holenliste.htm) | A built-in template wizard creates formula-driven points, edges and parts. The parameter editor and replay engine recalculate named geometry and supported modeling operations. See `TemplateWizard.cpp`, `ParametricDraft.cpp` and `ModelingHistory.cpp`. | Named draft points are draggable, with a live curve and edge preview and atomic Undo/Redo. Accepted offsets stay in the coordinate formulas and replay across sizes. The Construction Library saves editable project templates and creates independent copies. Insert Construction into Current Pattern copies pieces, objects, formulas, history, seams and grading into one undoable edit. Same-name measurements use the destination inputs. Other colliding names, generated seam IDs and piece indices are remapped. The source coordinate origin is retained. Explicit rebinding to different destination input names, instance synchronization and inter-project part inheritance remain open. |
| Curves, darts and panel lines | [New in GRAFIS 26](https://help.grafis.de/en-gb/Content/Neues_in_GRAFIS_26/Neues_in_G26.htm), [construction tool categories](https://help.grafis.de/en-gb/Content/Tools/Werkzeuge.htm) | Editable quadratic controls on free geometry, split and join, straight cuts and relief seams, yokes, slash-and-spread, pleats, gathering and constrained dart transfer. Supported operations on draft geometry replay by size. See `Modeling.cpp`, `ContourModeling.cpp` and `ModelingHistory.cpp`. | Cut, Relief Seam and Yoke accept straight and quadratic guides on closed line/quadratic contours. A guide can end inside a source curve. Both panels retain exact Bézier subdivisions and the recorded sewing edges have opposite directions. Object snapping locates points on free curves. The modeling history checks boundary topology again for each size. Bodice dart position and share of the waist suppression are parameters. General tangent controls, dart-to-princess conversion, general dart intake redistribution and automatic dart truing remain open. Curved source support is specific to the three panel-dividing operations. |
| Sleeves, collars and pockets | [Interactive sleeve](https://help.grafis.de/en-gb/Content/Interaktive_Konstruktionen/Interaktiver_Aermel_90.htm), [pocket and sleeve options](https://help.grafis.de/en-gb/Content/Neues_in_GRAFIS_23/Neues_in_interaktiven_Konstruktionen.htm) | Set-in, wide-hem and gathered puff sleeve variants, linked seam checks, flat collar, straight stand and facing. See `BodiceDraft.cpp` and `NeckFinish.cpp`. | The two-piece sleeve has an adjustable elbow curve and level, linked sewing edges and paired notches. The pointed collar preserves the neckline seam lengths and has a graded point extension. The rounded patch pocket includes a turnback and cutting allowance. Other two-piece sleeve constructions, curved/roll stands, general garment attachment, and welt, piped and in-seam pockets remain open. |
| Size tables and grading | [Grading](https://help.grafis.de/en-gb/Content/Gradieren/Gradieren.htm) | Per-size measurements override named draft inputs, then point offsets apply. The interface provides overlays and checks each size. See `Production.cpp`, `ProductionDialogs.cpp` and `MainWindow.cpp`. | Copy or replace measurement rows using three tab-separated columns. Shared measurement-system libraries, break-size interpolation and grade-rule exchange remain open. |
| Production patterns | [Preparing production styles](https://help.grafis.de/en-gb/Content/Schnittbildlegen/Produktionsmodell_erstellen.htm) | Closed contour checks, piece and edge-specific allowances, grain orientation, notches, seam links, cut counts, DXF and full-scale tiled PDF. See `PieceGeometry.cpp`, `PieceMarkings.cpp`, `SeamLinks.cpp`, `PatternDxf.cpp` and `PatternPdf.cpp`. | Product sheet lists part and size quantities from the current marker. Independent production styles assembled from multiple projects with source update tracking remain open. |
| Marker making | [Marker structure](https://help.grafis.de/en-gb/Content/Schnittbildlegen/Struktur_des_Schnittbildes.htm), [marker information](https://help.grafis.de/en-gb/Content/Schnittbildlegen/Schnittbildinformationen.htm) | Manual and heuristic automatic polygon placement, mixed size copies, mirrored pairs, edge folds, grain rotation constraints and material reporting. See `Production.cpp` and `ProductionDialogs.cpp`. | Clearance and two-axis fabric repeats constrain automatic placement and DXF/PDF export. Each placement can register its oriented outline corner or one named seam point. Paired copies can align corresponding seam points to a common repeat phase. Fold copies match along the length. Rectangular fabric flaws are shown in the marker, avoided by automatic placement and checked before export. Multiple simultaneous anchors, pair-specific phase offsets, per-piece buffers, shrinkage, multi-material jobs and cutting-machine integration remain open. |
| Product Information | [GRAFIS 26 release notes](https://grafis.de/release-notes) | No dedicated product record or product sheet. | The project stores the PDF title, free-text bill of materials and assembly instructions. The report can include cutting quantities and a table of size-specific draft inputs. The finished measurements table evaluates named draft-point distances by size. Structured BOM rows, images, sewn-garment paths and a general form designer remain open. |
| Workspace and file exchange | [GRAFIS interface](https://help.grafis.de/en-gb/Content/Grundlegendes/Grafis-Oberflaeche.htm), [vendor shortcuts](https://help.grafis.de/en-gb/Content/Grundlegendes/Wichtige_Regeln_fuer_Arbeit_mit_Grafis.htm) | Workflow menus, panels, shortcut customization, DXF preview and import/export, print PDFs, and native project recovery. See `Workflow.cpp`, `MainWindow.cpp` and `ProjectRecovery.cpp`. | Product menu and measurement clipboard exchange. Independent operator testing of task sequences remains open. Native GRAFIS `.MDL` is deferred. |

## Verification

- `ProjectPersistenceTest.cpp` opens legacy fixtures and verifies retention of product information and marker clearance in schema 15. Schema 16 is used only when projects store a fabric repeat or tech pack options.
- `ProfessionalModelingTest.cpp` moves a quadratic relief control repeatedly, checks rejection of an invalid move, checks matched curves and linked marks in base and graded sizes, and round-trips the result through SOFIT, DXF and tiled PDF. It also checks graded elbow, collar and dart geometry and exports a customized tech pack.
- `ProductionTest.cpp` checks automatic registration with two repeat axes and clearance, rejection of an off-repeat copy before export, and repeat persistence.
- `PatternWorkflowTest.cpp` exercises dragging a named construction point with mouse events, Undo/Redo, saving a construction, creating an editable instance, editing the tech pack, and setting marker repeats through the desktop UI.
- `ProfessionalModelingTest.cpp` checks usable pocket geometry, graded dimensions, and project, DXF and PDF output.
- `ProductionTest.cpp` checks clearance at the boundary, automatic placement, and cutting report validity.
- `PatternWorkflowTest.cpp` exercises product editing, undo/redo, PDF export, curve copying, clipboard exchange and saving marker settings through the desktop UI.
- The GitHub Actions Windows workflow builds with MSVC 2022 and Qt 6.8.3, runs CTest and the GUI at 150% scaling, launches the extracted portable ZIP, and installs, launches and uninstalls via Inno Setup. Workflow runs identify the tested commit. Independent Windows 10/11 operator trials, fit samples and physical cutting or plotter tests remain open.

## Command and scenario coverage for this cycle

Status describes the documented operation in SOFIT. **Implemented** means the stated SOFIT scenario is exercised. **Partial** means a concrete subset works. **Absent** identifies missing functionality. **Unverified** identifies a workflow whose end-to-end outcome was outside the available evidence. The scope is GRAFIS 26 CAD Apparel and Marker Making. The [GRAFIS toolbar reference](https://help.grafis.de/en-gb/Content/Tools/Werkzeuge.htm), [new features](https://grafis.com/new), [release notes](https://grafis.com/release-notes) and [marker feature reference](https://www.grafis.com/marker) define the comparison. Product options and add-ons are included where they touch the requested workflow.

| Documented command or operator scenario | Status | SOFIT evidence and remaining scope |
| --- | --- | --- |
| Library style insertion and parameter replay | Implemented | `PatternDocument::insertConstruction` and the Construction Library menu. `ProfessionalModelingTest` inserts a modeled module with duplicate symbols and generated seam links, changes grade inputs and round-trips through SOFIT, DXF and PDF. `PatternWorkflowTest` exercises insert, Undo and Redo. Insertion copies the complete construction, while marker settings and the product record belong to the destination. |
| Library previews, shape collections and part inheritance | Partial | Template wizard, editable construction files and imported internal dependencies. Library cards, favourites, shape switching, individual reusable dependency graphs and source-linked successors are absent. |
| Points, raster, lines, curves, circles and rectangles | Partial | Exact input, canvas grid, snapping, endpoint handles and quadratic controls. `PatternWorkflowTest` covers drawing and editing. GRAFIS-specific point/raster variants, higher-order curves and continuity constraints remain open. |
| Separate, link, lengthen, corners, parallel and curve manipulation | Partial | `Modeling.cpp`, `ContourModeling.cpp`, and `MainWindow.cpp` cover selected split/join, movement, offsets, and curve editing. No general trimming, extension, corner-construction family, or associative multi-curve offset tool. |
| Cut, relief and yoke from curved source edges | Implemented | `ContourModeling.cpp` retains the quadratic pieces, plus matching seams. `ModelingHistory.cpp` checks topology during replay. The professional test checks a yoke with an interior hit on the curved edge, named seams and graded round-trip/export. Native outlines are lines or quadratic curves. |
| Move, transformation turn, mirror, retransform, spread | Partial | Free-object transforms, recorded rotations, silhouette edits and straight slash/spread. `ProfessionalModelingTest` verifies model replay. General transformation of dependent parts and a recorded inverse workflow remain open. |
| Darts and hoods | Partial | A constrained dart can be transferred to a selected outlet, including closing an existing straight V. Existing bodice parameters control intake and mouth position. General intake split/merge, independent dart truing, dart-to-seam conversion and hood construction remain open. |
| Sleeve cap, collar neckline and pockets | Partial | `BodiceDraft.cpp` and `NeckFinish.cpp` provide template-derived seam measurements and links, cap ease and collar lengths by size, paired elbow marks and patch-pocket geometry. General solver for arbitrary armholes/necklines, cap-segment balance, roll stands and welt/in-seam pockets remain open. |
| Grade active/all parts, grade rules, size-table workflows | Partial | Grade replay, independent coordinate rules, overlays, clipboard rows and `Diagnose Pieces by Size`. JSON report checks every stored size for contour, allowance and seam tolerance. No active/successor dependency selection, shared measurement-system library, rule exchange or break-size interpolation. |
| Part assistant, front edge, alignment, marking lines, symbols and attributes | Partial | Pieces, construction edges, allowance regeneration, grain, labels and notches are covered in `PieceGeometryTest`, `ProfessionalModelingTest` and `PatternWorkflowTest`. Sewing symbols, front-edge families, tool-specific manufacturing parameters and independent production style tracking remain open. |
| Dimensions and finished measurement table | Partial | Size diagnostics report contour dimensions, perimeters and linked seam lengths. The tech pack evaluates point-to-point distances in every size, separately from draft inputs. Finished paths across several sewn pieces, ease rules, tolerances and linked measuring diagrams remain open. |
| Direction, grain, hand, fold, repeat and flaw-aware marker | Implemented | `Production.cpp`, `ProductionDialogs.cpp`, `ProductionTest` and `ProfessionalModelingTest` cover marker geometry, placement, graded seam repeat points, mirrored copies, rectangular flaws and repeat/overlap/export checks. Auto arrange is heuristic and handles one fabric at a time. |
| Template markers, fusing blocks, shrinkage, tubular spreading, cut-order planning and cutter control | Absent | Current marker supports explicit left/right folds, fixed orientations and saved copies. No tubular stack workflow, multi-material batches, reusable marker templates, cutter protocol or order-stack management. |
| Marker report, batch validation, print and export | Implemented | Cutting report and production JSON checks; saved-marker DXF and tiled A4 PDF are exercised by `ProductionAutomation.cmake`. Physical plotting, calibration, cutter output and cut quality are unverified. |
| Product Information and tech packs | Partial | `ProductInformation.cpp` and `ProfessionalModelingTest` cover information, free-text BOM and assembly, draft inputs, marker quantities and PDF. PDF includes named straight-line pattern measurements by size. Structured materials, images, sewn-path measurements, forms and PDF template editing remain open. |
| Pixel image import and tracing, stitch modules and programming | Absent | SOFIT provides geometry templates and formulas. It has no raster tracing workspace, stitch module or GRAFIS-style programming interface. Batch validation and marker export are available at the command line. |
| Import, reduced import, export and native exchange | Partial | Text AutoCAD DXF import preview and geometry exchange, plus PDF output. `DxfImportTest` and `ProjectPersistenceTest` cover fixtures. AAMA/ASTM semantics, RUL, reduced import modes and round-trip through an installed GRAFIS 26 are unverified or unsupported as detailed in the README. Native GRAFIS files including `.MDL` are deferred. |
| Independent operator and production validation | Unverified | No licensed GRAFIS session, independent Windows 10/11 operator study, fit sample, or plotter/cutter run was used. Compare application and packaging checks against the exact candidate's GitHub Actions runs. |


## 4.1 work in progress — comparison against 115c4e1

The remote main and the local baseline were at `115c4e194ca2891616f429fe9234a415a2eb80fe` on 2026-11-24. The [Windows baseline run](https://github.com/evtuhovklim82-commits/SOFIT/actions/runs/45357883145) passed CTest, the 150% GUI run, launch from portable ZIP, and Inno Setup compile, install, launch and uninstall. The [Linux baseline run](https://github.com/evtuhovklim82-commits/SOFIT/actions/runs/45357883132) passed with system Qt and Qt 6.8.3. Those results cover the baseline SHA. The updated source passed eight CTest entries on Linux with Qt 6.12.0, plus a separate offscreen GUI run with QT_SCALE_FACTOR=1.5. CPack generated the Linux tarball and checksum.

The [GRAFIS control bar reference](https://help.grafis.de/en-gb/Content/Grundlegendes/Grafis-Oberflaeche.htm) documents temporary measurement on Ctrl+M. The [interactive construction reference](https://help.grafis.de/en-gb/Content/Interaktive_Konstruktionen-Basiswissen/Interaktive_Oberflaeche.htm) documents F12 for the construction list, plus the Options, Areas and Values docks and drag-point constraints. SOFIT exposes its own stage menus and editable shortcuts. The grading and fabric layout dialogs provide Russian task help via «Справка» and F1. The desktop workflow test checks the layout help content and the configured F1 binding.

| Evidence | Coverage and limits |
| --- | --- |
| `checkGarmentProductionScenarios` in `ProfessionalModelingTest.cpp` | The test drafts an A-line pull-on skirt, linked trousers, a dress with facings, a shirt block, and a jacket block with a two-piece sleeve. It evaluates the base and two size/height rows and checks sewing contours, allowances and reported seam lengths. The taller variant goes through automatic arrangement, cut counts, project reopen, tech pack, pattern and marker PDFs, and DXF. It checks PDF structure and DXF entities. It does not drive all five workflows through the desktop or print to a device. The shirt and jacket fixtures use bodice templates and do not represent complete production styles with all their garment-specific details. |
| Skirt formulas and repeat registration | New skirt drafts expose hip ease and casing depth, plus hem flare for A-line skirts, to size-specific formula evaluation. The skirt scenario arranges and exports using a named hem point in the graded size with a two-axis repeat. Marker preview, checks and both export paths use the same preserved names. The regular grading output retains its existing handling of names. |
| Sleeve checks | The front armhole and sleeve cap differed from the target ease by 6.01 mm for the shirt fixture at 48/164. Armhole/cap pairs are outside the 2 mm assertion used for the other measured sewing pairs. The fabric layout and exports do not establish that the cap is suitable to sew. The continuation below records the new template cap solver. Production fitting remains open. |
| Automatic arrangement | A failed greedy ordering no longer prevents the search from considering another ordering or an existing valid layout. The multi-order search remains heuristic. No industrial fabric-use target, optimality or speed at production scale was established. |

General dart redistribution and truing, complex curve continuity, advanced size and height systems, additional construction modules, reusable operation graphs, broader Russian workflow coverage, and independent operator and physical production validation remain open. Native project and exchange formats were retained. Windows and Qt 6.8.3 execution for the changes above requires evidence from the updated revision.

## 4.1 continuation — sleeve and collar verification, 2026-11-24

The restored patch and the changes below are based on `115c4e194ca2891616f429fe9234a415a2eb80fe`. A live Git query returned that SHA for `origin/main`. The results below apply to the changed source tree.

| Construction and size | Front and back cap ease, target / measured | Front and back collar deviations |
| --- | --- | --- |
| Shirt base, 48/164 | 4.000 / 4.000 mm per section | 0.000 mm each |
| Shirt 48/176 | 5.000 / 5.000 mm per section | 0.000 mm each |
| Two-piece sleeve jacket base, 48/164 | 4.000 / 4.000 mm per section | 0.000 mm each |
| Two-piece sleeve jacket 48/176 | 5.000 / 5.000 mm per section | 0.000 mm each |

Measurements are rounded to 0.001 mm. The test asserts a 0.01 mm maximum deviation for each of the 24 reported measurements. This covers the shirt 48/164 front section previously reported at 6.01 mm deviation. The taller fixture also changes crown height, sleeve and bodice length, neckline width and front and back neckline depth. The collar equations were already present and were retained.

New sleeve drafts solve each quadratic cap control against the corresponding armhole length and saved target ease, including puff gathering when requested. The solution runs again with size measurements. It keeps the selected cap height, bicep width and a horizontal tangent at the crown. It limits the control position to 20–95% of the half-width. An unreachable target uses the nearest end of that range and remains visible as a deviation in the fitting check. The fitting dialog uses the saved target by default and supports a temporary override. Older projects retain their original geometry and formulas. Manual grading offsets applied after formula evaluation, arbitrary armholes, arbitrary necklines, real fit and a full industrial size grid remain outside this verification.

The Linux Release builds on Qt 6.8.3 and system Qt 6.12.0 each passed all eight CTest entries and a separate offscreen GUI run at 150%. CPack generated the Linux tarball and SHA-256 checksum from the Qt 6.12.0 build. The extracted executable returned `SOFIT 4.0.0` on `--version`; the partial update retains the baseline version identifier. The five professional scenarios cover A-line skirt, trousers, dress with facings, shirt block and jacket block with two-piece sleeve. Closed contours and allowances are checked on the base and both size/height rows. The taller row goes through marker arrangement, cutting quantities, native save and reopen, tech pack, pattern and marker DXF and PDF. The compatibility suite opens the existing legacy fixtures. New `capcontrol` formulas require an evaluator that supports the function.

The broader construction library, advanced dart and curve operations, industrial measurement-system and grading rules, industrial marker optimisation, reusable modeling operations, and the remaining panel, shortcut and Russian-language workflow work are still open. Independent operator workflow validation, physical fitting, plotting and cutting remain open. Windows MSVC 2022 / Qt 6.8.3, portable ZIP and Inno Setup have no result for the changed tree. No candidate was published. The earlier Windows and Linux CI results in this document refer to the baseline SHA.

## 4.1 continuation — intermediate sizes and marker computation, 2026-11-24

The grading editor can add a size by linearly interpolating explicit measurements between two different rows of the size table. The operator enters a unique target name and a percentage from the first size to the second. Both source sizes must have the same nonempty set of measurements. The result is stored as ordinary editable measurement rows and uses the existing parameter replay on acceptance. Grading point offsets are entered separately. The project schema and TSV format are unchanged. The new command and contextual instructions are in Russian. GRAFIS documents replaying the base construction using the measurements of active sizes and recommends checking extreme sizes during construction. Its guide describes separate size files and active-part, successor-part and all-part calculations. SOFIT's interpolation is a local size-table operation. [GRAFIS grading guide](https://help.grafis.de/en-gb/Content/Gradieren/Gradieren.htm)

The automatic marker builds the outline and clearance paths once per distinct cut and orientation, then reuses them across copies and search orders. The candidate search and final checks for overlap, fabric flaws, fold, orientation and repeat are retained. No benchmark at production scale or gain in fabric utilization was established in this pass.

| Check on the accumulated source | Result |
| --- | --- |
| Linux Release with Qt 6.8.3 | 8/8 CTest entries passed. Separate offscreen GUI workflow at 150% passed. |
| Linux Release with Qt 6.12.0 | 8/8 CTest entries passed. Separate offscreen GUI workflow at 150% passed. CPack generated the TGZ and checksum. The executable from the unpacked package returned SOFIT 4.0.0 with its existing version identifier. |
| Desktop grading | The workflow test imports two measurement sets, interpolates two measures at 25%, then saves and reopens the new size and verifies both values. |
| Fit geometry and compatibility | Both builds reran the 0.01 mm sleeve and collar assertions for the shirt and two-piece sleeve jacket in base, 48/164 and 48/176, the legacy .sofit fixtures and the DXF import fixtures. |
| Five garments | Automated coverage includes skirt, trousers, dress with facings, shirt and jacket with two-piece sleeve. The contour and allowance checks cover base and both sizes. At 48/176 the scenarios run marker arrangement, cutting quantities, native save and reopen, tech pack, and pattern and marker DXF and PDF exports. |
| Windows MSVC 2022 and Qt 6.8.3, portable ZIP and Inno Setup | Pending for the accumulated source. No CI branch was created. The older Windows evidence applies to 115c4e1. |

The remaining 4.1 scope includes a broader set of professional base constructions, dart redistribution and truing, curve continuity, shared measurement systems and industrial grading rules, marker optimization measured on production workloads, reusable modeling operations, and broader Russian interface and contextual help. Physical fitting, plotting and cutting remain unverified. The automated shirt and jacket examples are construction blocks. MDL and 4.2 remain outside this work.

## Продолжение SOFIT 4.1 от 24 ноября 2026 года

На начало сеанса рабочее дерево `/mnt/A/SOFIT` было чистым. Локальная `main` и сохранённая ссылка `origin/main` указывали на `cfe15e0`. Этот коммит уже содержал часть накопительного патча от `115c4e1`. Патч восстановлен трёхсторонним объединением в рабочей копии с сохранением изменений `cfe15e0`. Исходное дерево подключено только для чтения. Состояние удалённой ветки не подтверждено сетевым запросом из этого сеанса.

| Направление | Подтверждённое покрытие | Оставшиеся пробелы |
| --- | --- | --- |
| Разделение и объединение вытачек | Одна конструкционная вытачка делится между двумя выводами по заданным долям угла. Две конструкционные вытачки с общей вершиной сводятся в один вывод. Тесты измеряют равенство ножек, сохранение полного углового раствора и доли 35/65, проверяют замкнутость, открытие проекта и DXF/PDF. Квадратичные срезы сохраняются как кривые. Формульное разделение проверяется с пересчётом другого размера и повторным открытием проекта. | Входные ножки должны быть равными, линии раствора прямыми, а направляющие иметь общую точку. При разделении две вершины отступают на 20% пути от этой точки к серединам растворов. Применение к произвольным открытым вытачкам, больше двух выводов, ручной выбор глубины вершины, выравнивание среза после стачивания и общий редактор касательных остаются открыты. Разделение испытано с нулевым припуском. Припуск 3 мм в проверенном сценарии вызвал отказ геометрической проверки. Справка GRAFIS описывает полный и частичный перенос, до десяти выводов и отдельное укорачивание вытачек. [Вытачки и складки GRAFIS](https://help.grafis.de/en-gb/Content/Tools/Abnaeherr.htm) |
| Размер и рост | Мерки и при необходимости ΔX/ΔY для одинаковых точек интерполируются по четырём опорным строкам с независимыми долями размера и роста. Тест интерфейса проверяет 25% по размеру и 75% по росту, мерки 1050 и 631 мм, смещение (50, 10) мм и повторное открытие. Одномерный расчёт также проверен. | Операция добавляет одну редактируемую строку за вызов. Сохраняются вычисленные значения. Общие размерные системы, библиотека правил с обменом между моделями, типы фигур и пакетная генерация размерных рядов остаются открыты. GRAFIS документирует стандартные и индивидуальные таблицы, системы мерок и генерацию рядов. [Градация GRAFIS по таблицам мерок](https://help.grafis.de/en-gb/Content/Gradieren/Masstabellen.htm) |
| Раскладка | Три дополнительных сценария используют базу, 48/164 и 48/176, направленную ткань, зазор 5 мм и два дефекта. Измерены длины прежнего и расширенного поиска на одинаковых входных данных. Отдельный расчёт проверяет количество деталей каждого размера и ограничения готовой раскладки. Проверены DXF и PDF. Юбка дополнительно использует именованную точку раппорта. | Сценарии используют шаблонные детали и контролируемые дефекты. Нужны реальные производственные задания, проверка усадки, нескольких материалов, планирование настила, производительность около предела 200 копий и независимая операторская проверка. |
| Другие направления | В накопительном результате сохранены прилегающая юбка, перенесённые на квадратичный контур вытачки, повторное применение операций из проекта, справка и клавиатурные команды. | Полнота библиотеки конструкций, перенос произвольных подпоследовательностей с сопоставлением имён и полный русский интерфейс остаются открыты. |

Физическая примерка, печать в масштабе, настил и раскрой в этом сеансе не выполнялись. Полный паритет с GRAFIS 26 не установлен.

### Замеры и проверки накопительного дерева

| Трёхразмерная раскладка | Деталей | Полотно, мм | Прежний → расширенный поиск, мм | Qt 6.8.3 / Qt 6.12.0, с |
| --- | ---: | ---: | ---: | ---: |
| Юбка А-силуэта с раппортом 20 × 30 мм | 6 | 1500 | 2620,00 → 2620,00 | 0,002 / 0,003 |
| Брюки | 12 | 1500 | 3740,02 → 3740,02 | 15,510 / 17,092 |
| Основа жакета с двухшовным рукавом | 24 | 1450 | 1170,02 → 1170,02 | 26,857 / 29,824 |

Обе версии Qt дали одинаковые длины. На этих трёх наборах расширенный поиск сохранил прежний расход. Отдельный повтор предыдущего задания из `linked-trousers.sofit` на Qt 6.12.0 снова дал 4320,00 → 3270,00 мм для 12 деталей, экономия 24,31%. Прогон занял 21,364 с. Время включает прежние и добавленные порядки поиска и относится к данному оборудованию и условиям запуска.

| Проверка | Результат |
| --- | --- |
| Linux Release, Qt 6.8.3 | 8/8 CTest. Отдельный тест интерфейса при `QT_SCALE_FACTOR=1.5`, Qt offscreen, прошёл. |
| Linux Release, системный Qt 6.12.0 | 8/8 CTest. Отдельный тест интерфейса при `QT_SCALE_FACTOR=1.5`, Qt offscreen, прошёл. |
| Совместимость и окат | Обе сборки открыли старые .sofit, прошли импорт эталонных DXF и сквозные проверки DXF/PDF. Передний окат основы рубашки 48/164 дал 4,000 мм при цели 4,000 мм. |
| Linux TGZ на Qt 6.12.0 | Контрольная сумма прошла. Распакованное приложение сообщило версию 4.0.0 из текущего CMake и открыло пять комплектных примеров, включая два формата v14. |
| Windows MSVC 2022 и Qt 6.8.3 | Сборка, полный набор тестов, интерфейс при 150%, запуск из portable ZIP и цикл Inno Setup install/run/uninstall остаются без результата для этого дерева. В сеансе нет Windows-раннера с требуемым окружением. Существующий GitHub workflow не запускался для этого дерева. |
| Git и поставка | Изменения сохранены двумя патчами для разных баз. Патч от `cfe15e0` содержит только недостающее для текущей `main`. Накопительный патч от `115c4e1` включает предыдущие и новые изменения. Оба независимо проверены на своих базах и дают одинаковое дерево файлов. Коммитов и push в этом сеансе не было. |

## Начало SOFIT 5.0 Professional от 24 ноября 2026 года

База этого цикла `cfe15e0a5f376fc497f9f6e49ac186f1d7839e03`. Патч из сеанса 9 на `115c4e1` воспроизводит дерево этой базы. Частичный патч из сеанса 11 поверх `cfe15e0` и накопительный из того же сеанса поверх `115c4e1` дают побайтно одинаковое дерево. Замеры в предыдущих разделах относятся к соответствующим историческим деревьям.

| Направление | Покрытие в рабочем дереве 5.0 | Оставшиеся пробелы |
| --- | --- | --- |
| Перенос мерок между моделями | В таблице градации команда «Добавить мерки из буфера» принимает прежний трёхколоночный TSV. Можно дополнить существующий размер или добавить новый. При дубликате пары «размер и мерка» либо недопустимом значении вставка целиком отклоняется. Правила градации и ссылки на размеры в раскладке сохраняются. Тест интерфейса проверяет вставку, отказ при конфликте, сохранение проекта, повторное открытие и использование исходного размера в раскладке. | Общий каталог размерных систем, связь модели с обновляемой таблицей и пакетная генерация размерных рядов остаются открыты. |

| Проверка рабочего дерева | Результат |
| --- | --- |
| Linux Release, Qt 6.8.3 и системный Qt 6.12.0 | По 8/8 CTest. Тест интерфейса при 150% прошёл на обеих сборках с Qt offscreen. |
| Совместимость и производственные сценарии | Проверены старые .sofit, импорт DXF, пять сценариев изделий, пересчёт размеров, раскладка, сохранение и повторное открытие, экспорт DXF/PDF. |
| Linux TGZ, системный Qt | SHA-256 совпала. Приложение из архива открыло пять комплектных проектов, включая два проекта v14. |
| Windows 10/11, MSVC 2022 и Qt 6.8.3 | Для этого дерева результатов пока нет. Открыты сборка и тесты, интерфейс при 150%, portable ZIP и цикл Inno Setup. |

## Продолжение SOFIT 5.0 Professional от 24 ноября 2026 года

База \`cfe15e0a5f376fc497f9f6e49ac186f1d7839e03\`. Входной накопительный патч 5.0 повторно применён к независимому экспорту базы. Результат побайтно совпал с рабочим деревом перед изменениями этого сеанса.

| Направление | Реализовано и проверяется в этом дереве | Границы покрытия |
| --- | --- | --- |
| Параметрическая библиотека | Мастер строит цельнокроеную рубашечную манжету по обхвату запястья, прибавке, нахлёсту и готовой ширине. Деталь имеет сгиб, линию нахлёста, точечные ориентиры застёжки, долевую, припуск 10 мм и норму две детали. Тест проверяет другой размер, контур, .sofit и экспорт лекала DXF/PDF. | Автоматическое сопряжение с произвольным рукавом и отдельные производственные символы для петли и пуговицы остаются открыты. |
| Вытачки, кривые, складки | Доля первого вывода разделённой формульной вытачки редактируется в дереве построения. Раздвижка, складка и сборка поддерживают прямой разрез на квадратичном контуре. Точное деление и перенос кривых сохраняют их форму. Тесты проверяют величину раздвижки, метки, припуск, другой размер и DXF/PDF. | Ограничение одного разреза за операцию сохраняется. Многоосевая драпировка, более двух выводов, выравнивание стачанного среза и общий редактор непрерывности остаются открыты. [Вытачки и складки](https://help.grafis.de/en-gb/Content/Tools/Abnaeherr.htm) · [Раздвижка](https://help.grafis.de/en-gb/Content/Tools/Sperren.htm) |
| Размерно-ростовая градация | Таблицы мерок можно сохранять и добавлять из UTF-8 TSV. Загрузка проверяет весь набор и оставляет имеющиеся правила и размеры при конфликте. Тест интерфейса использует кириллицу, CRLF, запись файла и повторное открытие .sofit. | Формульные имена в целевом проекте должны совпадать для пересчёта. Связанные каталоги, типы фигур, пакетная сетка и обмен правилами смещения остаются открыты. [Таблицы мерок GRAFIS](https://help.grafis.de/en-gb/Content/Gradieren/Masstabellen.htm) |
| Раскладка | Дополнительные позиции поиска используют пространство слева и выше прямоугольных дефектов с учётом сетки раппорта. Отдельный сценарий с четырьмя дефектами проверяет строгое снижение длины и независимую проверку готовой раскладки. | Это эвристика. Многоматериальные задания, усадка, оборудование и промышленные целевые показатели требуют самостоятельной реализации и проверки. [GRAFIS Marker Making](https://www.grafis.com/marker) |
| Производственные лекала и техпакеты | Отчёт по размерам открывает свойства выбранной базовой детали и обновляется после правки. В PDF техпакета показаны норма на комплект, количество в раскладке и показатели проверенной раскладки. При ошибке раскладки PDF указывает её статус. | Производное лекало с наследованием, мастер закрытия углов, независимые швейные символы и редактируемые формы техпакета остаются открыты. [Part Assistant GRAFIS](https://help.grafis.de/en-gb/Content/Verarbeitung/Der_Teile-Assistent.htm) |
| Интерфейс и справка | Диагностика по размерам получила настраиваемое сочетание Ctrl+Shift+I и переход к свойствам детали. Русские подсказки описывают новые рабочие операции. | Полная русификация и сравнение последовательностей действий с оператором GRAFIS 26 остаются открыты. |

Формат .sofit сохраняет прежнюю схему. Работы по .MDL отложены.

| Проверка этого дерева | Результат |
| --- | --- |
| Linux Release, Qt 6.8.3 и 6.12.0 | По 8/8 CTest. На обеих сборках отдельно прошёл тест интерфейса с масштабом 150% в Qt offscreen. |
| Сквозные сценарии | Пять изделий, пересчёт размеров, раскладка, техпакет, .sofit и DXF/PDF. Отдельно проверены манжета, квадратичные контуры после раздвижки, складки и сборки, эталонный импорт DXF и старые .sofit. |
| Оптимизация на контрольной геометрии | Одна деталь 40 × 40 мм, полотно 100 мм и четыре дефекта. Длина сократилась со 140 до 60 мм. Проверка готовой раскладки прошла. Этот сценарий не измеряет производительность промышленного настила. |
| Техпакет | Из PDF извлечены норма 2, размещённая 1, длина 40 мм, ширина ткани 100 мм и использование ткани 40%. Для детали за пределами ткани PDF сообщил об ошибке раскладки. |
| Linux TGZ | SHA-256 совпала. Из распакованного пакета открылись пять примеров, включая два .sofit v14. |
| Windows 10/11 с MSVC 2022 и Qt 6.8.3 | Проверок этого дерева нет. Нужна среда Windows для сборки, запуска набора тестов при 150%, проверки Portable ZIP и установки, запуска и удаления через Inno Setup. |

## SOFIT 5.0 Professional Complete. Состояние рабочего дерева от 24 ноября 2026 года

База `cfe15e0a5f376fc497f9f6e49ac186f1d7839e03`. Последний входной накопительный патч повторно применён к независимому экспорту базы. Дерево совпало с рабочей копией до начала разработки. Статус ниже относится к конкретным сценариям SOFIT.

| Направление / сценарий | Статус | Реализация и границы |
| --- | --- | --- |
| Библиотечные шаблоны и повторное использование зависимостей | Частично реализовано | Шаблоны юбок, брюк, платья, основ рубашки и жакета, рукавов, воротников, манжеты и накладного кармана. Сохранение и вставка параметрических блоков с переназначением внутренних имён. Остаются полные промышленные конструкции, больше вариантов карманов и наследование обновлений источника. |
| Вытачки, раздвижка, рельефы, кокетки, складки и сборки | Частично реализовано | Перенос открытой вытачки, разделение на два вывода и объединение при общей вершине. Рельеф, кокетка и раздвижка сохраняют поддерживаемые квадратичные контуры. Связи швов проверяют длины и переносят надсечки в поддерживаемых шаблонах. Остаются общий подбор сопряжённых кривых, управление непрерывностью, выравнивание стачанного контура и многовыводный перенос. |
| Многоосевая драпировка | Отсутствует | Раздвижка и складка с одной направляющей покрывают локальные сценарии. Интерактивный многолинейный инструмент с независимыми растворами остаётся открытым. |
| Пакетная размерно-ростовая интерполяция | Реализовано | Несколько новых размеров за один вызов по четырём опорным строкам. Каждая целевая строка задаёт независимые доли размера и роста. При совпадающих ключах точек интерполируются также ΔX/ΔY. Существующий пересчёт применяется при подтверждении редактора. Тесты покрывают несовместимые наборы мерок и сохранение корректной группы. [Таблицы мерок GRAFIS](https://help.grafis.de/en-gb/Content/Gradieren/Masstabellen.htm) |
| Размерные системы и правила | Частично реализовано | Пользовательские таблицы UTF-8 TSV, формульные переопределения и смещения точек, включая контрольные точки квадратичных кривых. Остаются каталог систем, связь с обновляемыми таблицами, типы фигур и независимые правила на уровне целой кривой или детали. |
| Раскладка нескольких моделей | Реализовано для подготовленных снимков | Команда импорта добавляет выбранный размер и количество комплектов каждого .sofit в общую раскладку. Совпадающие названия размеров разных моделей не связывают их геометрию. Поддерживаемые связанные припуски и надсечки переносятся на срезы. Копии статические и не пересчитываются при правке исходной модели. Сценарий проверяет общую авторасстановку, сохранение, повторное открытие и DXF/PDF. |
| Промышленный nesting и настил | Частично реализовано | Зазор, направление ворса, раппорт, дефекты, сгиб и проверка готовой раскладки. Интерфейс показывает длительность поиска и сравнение длин, если есть результат для базовой эвристики. Остаются многоматериальные задания, усадка, планирование настилов и интеграция с раскройным оборудованием. [GRAFIS Marker Making](https://www.grafis.com/marker) |
| Производственные лекала и Part Assistant | Частично реализовано | Контроль контура и припуска по размерам, свойства детали из диагностики, связанные надсечки и контрольные измерения, отчёт и техпакет. Раскладка сохраняет поворот и зеркальность надсечек. DXF пишет линии на слой NOTCH. PDF рисует штрихи на раскладке. Остаются наследование производственных деталей, мастер обработки углов, полный каталог символов и редактор форм техпакета. [Part Assistant GRAFIS](https://help.grafis.de/en-gb/Content/Verarbeitung/Der_Teile-Assistent.htm) |
| Панели, клавиши и рабочий процесс | Частично реализовано | Панели этапов, интерактивное редактирование, переназначение клавиш и контекстная справка. Новые команды группы размеров и импорта моделей сопровождаются русскими подписями. Полная локализация и независимая проверка привычных операторских последовательностей остаются открыты. |
| Windows и физическое производство | Не проверено | Для этого дерева нет прогона Windows 10/11 с MSVC 2022 и Qt 6.8.3, теста при 150%, Portable ZIP и Inno Setup. Физическая примерка, печать, настил и раскрой остаются без подтверждения. |

### Измерение раскладки

Три прогона на Linux, Qt 6.12.0, ширина 1450 мм, две прямоугольные формы, направленная ткань, зазор 5 мм, раппорт 20 × 30 мм и два дефекта. Все результаты прошли независимую проверку раскладки. Длины повторились между прогонами.

| Копий | Базовый поиск, мм | Расширенный поиск, мм | Время поиска, мс |
| ---: | ---: | ---: | ---: |
| 20 | 1144 | 1144 | 18 |
| 60 | 2674 | 2674 | 165–168 |
| 120 | 4834 | 4834 | 625–648 |
| 200 | 7894 | 7894 | 1743–1801 |

Ответ на производственную задачу зависит от контуров. В отдельном регрессионном примере из 12 копий длина сократилась с 4012,05 до 3949,04 мм. Прогоны с прямоугольниками оценивают повторяемость и время на простых контурах. Предел масштабируемости на реальных заказах не установлен. Native .MDL отложен. Полный паритет с GRAFIS 26 не подтверждён.

### Проверки текущего дерева

| Проверка | Результат |
| --- | --- |
| Linux Release, Qt 6.8.3 и 6.12.0 | По 8/8 CTest. Отдельно по 1/1 GUI при 150% в Qt offscreen. |
| Регрессии изделий и форматов | Пять сквозных сценариев изделий, наследуемые .sofit, эталонный импорт DXF, экспорт DXF/PDF, групповой расчёт размеров, смешанная раскладка и слой надсечек. |
| Linux TGZ | SHA-256 совпала. Распакованное приложение открыло пять примеров, включая два проекта v14. Сборка сохранила текущую строку версии 4.0.0. |
| Windows 10/11, Portable ZIP и Inno Setup | Не проверено. Локальной подготовленной среды нет, GitHub Actions для этого дерева не запускался. |

## Следующий этап SOFIT 5.0 Professional от 24 ноября 2026 года

Рабочая копия восстановлена входным накопительным патчем от `cfe15e0a5f376fc497f9f6e49ac186f1d7839e03`. Исходный аудит побайтно совпал с версией в восстановленном дереве.

| Сценарий | Поддержка в этом этапе | Открытые задачи |
| --- | --- | --- |
| Раздвижка по нескольким направляющим, складки и кривые | Диалог задаёт порядок именных прямых направляющих и ширину каждого разреза. Предпросмотр строит контур и разметку. Последний разрез поддерживает встречную складку или сборку. Операция сохраняет последовательность зависимостей от формул, проверяет все размеры и входит в Undo/Redo одним действием. Поддерживаются квадратичные контуры. | Каждый следующий разрез должен пересекать уже полученный контур. Произвольные криволинейные направляющие, автоматический перенос направляющих после предыдущего разреза, разметка нескольких складок и формулы растворов остаются открытыми. |
| Размерные системы и промышленные ряды | Папка библиотеки запоминается в настройках. Пользователь сохраняет и повторно импортирует собственные наборы мерок UTF-8 TSV. Ряд по двум числовым диапазонам строит целевые строки для пакетной градации по четырём опорам. | Встроенные стандарты и типы фигур, метаданные систем и синхронизация мерок с источником остаются открытыми. GRAFIS документирует системы, типы фигур, отдельные таблицы и генерацию стандартных рядов. [Справка GRAFIS по таблицам мерок](https://help.grafis.de/en-gb/Content/Gradieren/Masstabellen.htm) |
| Part Assistant | Отдельный вход объединяет диагностику контуров, припусков, надсечек, долевой, готовых контрольных мерок и количества деталей. Доступны свойства детали, редактор припуска выбранного ребра, редактирование контрольных мерок, параметры и экспорт техпакета. | Остаются наследование, пошаговая обработка углов и полный набор технологических символов, включая марки сверления. Эти операции документированы у GRAFIS. [Справка GRAFIS Part Assistant](https://help.grafis.de/en-gb/Content/Verarbeitung/Der_Teile-Assistent.htm) |
| Промышленная раскладка | Сквозной тест строит раскладку двух моделей с двумя и одним комплектом, одинаковым именем размера, дефектами, зазором и раппортом. Проверены количества, отказ при переносе детали на дефект, повторное открытие и штатный экспорт DXF/PDF. | Настил, усадка и многоматериальные заказы остаются открытыми. GRAFIS документирует смешанные модели, дефекты и учёт усадки. [GRAFIS Marker Making](https://www.grafis.com/marker) |
| Выпуск | Предыдущие результаты Linux сохраняются в истории аудита. | Требуются Windows 10/11, MSVC 2022, Qt 6.8.3, проверка GUI при 150%, запуск Portable ZIP и цикл Inno Setup. |

На новом дереве повторены три прогона прежнего прямоугольного набора с дефектами под Qt 6.12.0. Длины базового и расширенного поиска совпали на каждом объёме.

| Копий | Длина, мм | Время, мс |
| ---: | ---: | ---: |
| 20 | 1 144 | 18–19 |
| 60 | 2 674 | 157–165 |
| 120 | 4 834 | 610–634 |
| 200 | 7 894 | 1 670–1 730 |

Контроль из 12 деталей снова дал 4 012,05 → 3 949,04 мм. Время на этом оборудовании описывает именно контрольный набор.

| Проверка текущего дерева | Результат |
| --- | --- |
| Linux Release, Qt 6.8.3 | 8/8 CTest. Отдельный сценарий GUI при 150% в Qt offscreen прошёл. |
| Linux Release, Qt 6.12.0 | 8/8 CTest. Отдельный сценарий GUI при 150% в Qt offscreen прошёл. |
| Покрытие | Пять сквозных изделий, сохранение и повторное открытие старых .sofit, DXF/PDF, пакетная градация, редактор припуска из Part Assistant, отмена и повтор двух разрезов, смешанная раскладка с дефектами. |
| Linux TGZ | SHA-256 совпала с контрольной суммой CPack. Распакованное приложение открыло все пять комплектных проектов, включая два v14. Версия пакета 4.0.0. |
| Windows 10/11 | Остаётся обязательной проверкой выпуска. Подготовленной Windows-среды для данного дерева не было. |

## Продолжение SOFIT 5.0 Professional от 24 ноября 2026 года

| Сценарий | Подтверждаемое покрытие SOFIT | Оставшийся объём |
| --- | --- | --- |
| Криволинейная раздвижка и драпировка | Раздвижка, встречная складка и сборка принимают прямые и квадратичные направляющие. Концы лежат на контуре. Ширина задаёт перенос по нормали к хорде. Для складки и сборки копии направляющей сохраняют точный квадратичный контроль. Диалог последовательности показывает эту разметку. Регрессия проверяет раздвижку по прямой и затем по кривой, пересчёт размера, отказ при выходе кривой из контура и обмен .sofit, DXF и PDF. | Составные направляющие, автоматический перенос следующих направляющих, формулы растворов и складки на нескольких разрезах. |
| Встроенные размерные данные | В редакторе градации можно добавить любую из 356 типовых фигур таблицы 3 ГОСТ 31396-2009. Рост и обхваты груди и бёдер сохраняются в мм с номером группы. Сценарий GUI проверяет число фигур в каждой группе, отсутствие недопустимой комбинации, выбор 164–92–90 и повторное открытие проекта. [Росстандарт](https://protect.gost.ru/gost/details/7d9919ad-6695-41c5-93e1-8224eaf3117d), [скан таблицы 3](https://meganorm.ru/Data2/1/4293808/4293808596.pdf) | Другие стандарты, дополнительные мерки и обновление при смене редакции. [Справка GRAFIS по таблицам мерок](https://www.help.grafis.de/de-de/Content/Gradieren/Masstabellen.htm) |
| Part Assistant и сверление | Ассистент добавляет точку по координатам и показывает её в диагностике по размерам. При вводе она проверяется для базы и сохранённых размеров. Точка сохраняется в .sofit, DXF использует отдельный слой SOFIT_DRILL, а PDF настила рисует метку в масштабе. При повороте и зеркальном размещении точка преобразуется вместе с деталью. | Полный каталог обозначений, формульные координаты, мастер обработки углов, наследование и редактор форм техпакета. [Справка GRAFIS Part Assistant](https://help.grafis.de/en-gb/Content/Verarbeitung/Der_Teile-Assistent.htm) |
| Промышленный настил | Проверяется прежний сценарий двух моделей на одном полотне. | Материалы с независимыми настройками, компенсация усадки и планирование слоёв настила остаются открытыми. [GRAFIS Marker Making](https://www.grafis.com/marker) |

Данные предыдущих этапов сохранены выше. Windows 10/11, Portable ZIP и Inno Setup для этого дерева остаются без подтверждения. MDL остаётся вне объёма.

| Проверка текущего дерева | Результат |
| --- | --- |
| Linux Release на Qt 6.8.3 и 6.12.0 | По 8/8 CTest. Отдельный GUI-прогон при 150% через Qt offscreen прошёл на каждой версии. |
| Изделия, .sofit, DXF/PDF | Пройдены пять сохранённых сквозных сценариев изделий. Набор также проверяет импорт DXF, старые .sofit и DXF/PDF настила. Тест сверления проверяет преобразование координат, DXF POINT и отказ DXF/PDF для точки вне контура. |
| Linux TGZ | SHA-256 совпала. Все пять комплектных проектов, включая два v14, открылись из распакованной сборки при 150%. |

## Финальный цикл кандидата SOFIT 5.0 Professional от 24 ноября 2026 года

База `cfe15e0a5f376fc497f9f6e49ac186f1d7839e03`. Приведённые выше результаты относятся к соответствующим предыдущим деревьям. Статус выпуска текущего кандидата — открыт.

| Направление | Подтверждённое изменение или результат | Открытые задачи |
| --- | --- | --- |
| Составная направляющая | Раздвижка, встречная складка и сборка принимают упорядоченную цепочку до 32 прямых и квадратичных сегментов. Концы лежат на контуре, промежуточные узлы внутри. Сегменты разворачиваются для соединения, геометрия проверяется до применения. Сохраняются исходные связи и точная разметка каждого сегмента. | Сопряжение по заданной касательной, настоящий перенос последующих направляющих и несколько складок в одном проходе. |
| Формулы раствора | Число или формула в миллиметрах для именной раздвижки, складки и сборки. Формула использует тот же вычислитель, что чертёж, с мерками текущего размера. Есть ввод в диалоге последовательности и редакторе истории. Сохранение и повторное открытие сохраняют выражение. Недопустимый раствор в одном размере блокирует операцию. | Для последовательности сохранена разметка последнего разреза. Для ширины используется нормаль к хорде. |
| Размерные системы | Поддерживаются все шесть групп таблицы 3 ГОСТ 31396-2009, пользовательский TSV и пакетная интерполяция. Подтверждены официальные карточки других стандартов. | Полнота таблиц, метрологическая сверка строк и подтверждение возможности распространения новых наборов. [ГОСТ 31396-2009](https://protect.gost.ru/gost/details/7d9919ad-6695-41c5-93e1-8224eaf3117d) · [ГОСТ 31399-2009](https://protect.gost.ru/gost/details/20dd2bb7-b98c-4f14-b1c4-7786b8a484cb) · [Детские ГОСТ 17916-86 и 17917-86](https://protect.gost.ru/gost?month=0&page=635&search=&year=0) |
| Part Assistant | Повторные проверки диагностики припусков, надсечек, долевой, параметров и сверления. | Полный каталог контурных и точечных символов, наследование детали, обработка углов, редактор форм техпакета. [Part Assistant GRAFIS](https://help.grafis.de/de-de/Content/Verarbeitung/Der_Teile-Assistent.htm) · [Символы](https://help.grafis.de/de-de/Content/Verarbeitung/Symbole.htm) |
| Многоматериальный настил | Копии из текущего задания группируются по материалу детали. Для каждой группы отдельно задаются ширина, число слоёв и настройки раскладки. Обычный валидатор проверяет раскладку и состав, парность и сгибы копий сохраняются. Готовый комплект создаётся целиком и содержит независимые SOFIT, DXF и PDF и ведомость длины слоя и расхода в метрах. GUI-тест проверяет 0,0900 м подкладки и 0,0800 м основной ткани, повторное открытие и экспорт. | Компенсация усадки, выбор материала отдельной копии одной и той же детали, централизованное сохранение задания для дальнейшего редактирования, несколько настилов на один материал и связь с оборудованием. Материал назначается детали целиком, исходный проект остаётся на месте. [GRAFIS Marker Making](https://www.grafis.com/marker) |
| Прочие пробелы | История аудита фиксирует ранее проверенные конструкции и инструменты. | Дополнительные промышленные шаблоны и карманы, наследование обновлений источника, сопряжение произвольных кривых, многовыводный перенос вытачек, независимые правила градации целой кривой или детали, полная локализация и операторская приёмка. |
| Windows 10/11 | Существующий GitHub Actions описывает сборку MSVC 2022 / Qt 6.8.3 и Portable ZIP, а также установку, запуск и удаление Inno Setup на `windows-2022`. В этой среде подходящих систем Windows 10/11 не обнаружено, GitHub по SSH/HTTPS недоступен, новый запуск CI не подтверждён. | Проверить обе клиентские ОС при 150% и пройти заявленную матрицу на собранном кандидате. [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) |
| Профессиональная приёмка | Пять редактируемых проектов с базой и двумя размерами, контрольные DXF/PDF и диагностика, старые файлы и DXF с дюймовыми единицами, инструкция и незаполненный протокол. | Реальный конструктор GRAFIS 26, сравнение результатов, печать и макеты, замечания, повторный прогон и решение о приёмке. |

Обычная Linux-сборка использует версию из базового CMake, `4.0.0`. Обновление номера пакета относится к выпуску. MDL вне запрошенного объёма.

| Проверка текущего дерева | Результат |
| --- | --- |
| Linux Release, Qt 6.8.3 | 8/8 CTest. Тест интерфейса при 150% через Qt offscreen прошёл. |
| Linux Release, Qt 6.12.0 | 8/8 CTest. Тест интерфейса при 150% через Qt offscreen прошёл. |
| Linux, экранный сеанс | Попытка запуска через XCB остановилась на подключении к дисплею `:0`. Результаты GUI относятся к автоматизированным прогонам offscreen. |
| Пять изделий и обмен | Пять сквозных сценариев по три размера. На обеих сборках вновь открыты пять файлов для приёмки, проверено чтение старых .sofit и DXF, повторно получены DXF/PDF. DXF и отчёты JSON совпали между версиями Qt. |
| Смешанные заказы | Проверены три размера на материале с двумя дефектами и зазором 5 мм. Юбка — 6 копий, 2620 мм, с раппортом. Брюки — 12 копий, 3740,02 мм. Жакет — 24 копии, 1170,02 мм. Независимая проверка валидности и количества пройдена. Ни сокращения длины в этих трёх наборах, ни показателей на реальном промышленном заказе по этому результату не заявлено. |
| Linux TGZ, Qt 6.12.0 | SHA-256 совпала. Из распакованного пакета при 150% в Qt offscreen запущены пять примеров, включая два v14, и пять проектов для приёмки. |

Накопительный патч предназначен для чистой базы. Локальные HEAD, main и ссылка origin/main совпали с `cfe15e0`. Изменения выполнены на отдельном экспорте. Коммит и push отложены до выполнения условий выпуска.

## SOFIT 5.0 Final — сверка кандидата от 24 ноября 2026 года

База `cfe15e0`. Строки предыдущих этапов сохранены как история. Ниже указан объём последнего дерева. Справка GRAFIS описывает возможности поставщика. Проверка команд в установленном GRAFIS 26 остаётся частью профессиональной приёмки. [Изменения GRAFIS 26](https://help.grafis.de/en-gb/Content/Neues_in_GRAFIS_26/Neues_in_G26.htm) · [Журнал версий](https://grafis.de/release-notes)

| Направление | Поддержка кандидата | Открыто |
| --- | --- | --- |
| Раздвижка и сложное моделирование | Именованные непересекающиеся направляющие переводятся вместе со своей половиной детали. Для каждого разреза выбираются раздвижка, встречная складка или сборка и формула раствора. Существующие метки складок переносятся на следующих разрезах. Операция применяется целиком, проверяет размеры и сохраняет последовательность. Отдельный инструмент выравнивает касательную ручной квадратичной кривой по смежной прямой или кривой. | Пересекающиеся разрезы, переменное число разрезов по размерам, общий редактор сопряжений с формульными ограничениями, моделирование уровня произвольных рукавов, воротников и карманов, обобщённый многовыводный перенос вытачек и построение колпачка складки. [Drape and shape](https://help.grafis.de/en-gb/Content/Tools/Modellieren.htm) · [Способы раздвижки](https://help.grafis.de/en-gb/Content/Tools/Sperren.htm) · [Редакторы кривых](https://help.grafis.de/en-gb/Content/Tools/Kurven.htm) |
| Размерные стандарты | Встроенный проверенный фрагмент женской группы 0 и ранее реализованные пользовательские TSV и размерно-ростовая интерполяция. Сверены карточки женского, мужского и двух детских стандартов, включая сведения о поправках. | Для расширения набора требуются полные сверенные строки нужной редакции, соответствие единиц, кодов и типов фигур, а также подтверждённые условия распространения. Новые таблицы в этом дереве отсутствуют. Также открыты связь с редакцией таблицы и правила на целую кривую или деталь. [Системы мерок GRAFIS](https://help.grafis.de/en-gb/Content/Gradieren/Masstabellen.htm) · [Женские фигуры](https://protect.gost.ru/gost/details/7d9919ad-6695-41c5-93e1-8224eaf3117d) · [Мужские фигуры](https://protect.gost.ru/gost/details/20dd2bb7-b98c-4f14-b1c4-7786b8a484cb) · [Девочки](https://protect.gost.ru/gost/details/06788891-adac-49c3-b3e1-168ab7c4fb06) · [Мальчики](https://protect.gost.ru/gost/details/f9c14536-dd81-443d-af49-cffd76c2b032) |
| Part Assistant | Новая производственная деталь может наследовать исходные формульные рёбра через общие точки. Пересчёт исходных точек обновляет оба контура. Добавлены внутриконтуральные крест, окружность, петля и знаки раппорта по основе и утку с размером и поворотом. Геометрия сохраняется в проекте и попадает в DXF/PDF настила. | Наследование процедурно полученных контуров и отдельных объектов между проектами, последовательный мастер, семейство углов, размещение знаков на припуске, остальные контурные и точечные обозначения, специализированные линии, формы техпакета и полный семантический обмен символами. [Part Assistant](https://help.grafis.de/en-gb/Content/Verarbeitung/Der_Teile-Assistent.htm) · [Перечень символов](https://help.grafis.de/de-de/Content/Verarbeitung/Symbole.htm) |
| Настил и усадка | Усадка задаётся по утку и основе для каждой раскладки. Габариты, знаки и опорные точки раппорта масштабируются в осях полотна. Расход, столкновения и экспорт считают геометрию до обработки. Выбор материала и номера настила доступен для каждой копии или сопоставленной пары. Ведомость фиксирует материал, номер, компенсацию, длину слоя и расход. | Данные задания в исходном проекте, отдельные режимы усадки на деталь и буфер, компенсация растяжения, производственные лимиты настила, планирование слоёв на весь заказ, интеграция с раскройным оборудованием. [Усадка GRAFIS](https://help.grafis.de/en-gb/Content/Schnittbildlegen2/Einlaufen.htm) · [Планирование заказов](https://help.grafis.de/en-gb/Content/Lagenplaner/Vorbereiting.htm) · [Marker Making](https://www.grafis.com/marker) |
| Параметрическая библиотека и интерфейс | При вставке конструкции явная привязка связывает входную мерку с меркой назначения, даже при разных именах. Переименование применяется к формулам геометрии и моделирования и к данным градации. Новые действия доступны в библиотеке, Part Assistant, моделировании и редакторе раскладки. | Карточки и предпросмотр библиотеки, обновление экземпляров из источника, коллекции форм, полная локализация, независимая оценка операторского процесса и GUI в экранной сессии. [Библиотека GRAFIS](https://help.grafis.de/en-gb/Content/Module/Die_Gestaltung_der_Holenliste.htm) · [Интерфейс](https://help.grafis.de/en-gb/Content/Grundlegendes/Grafis-Oberflaeche.htm) |
| Выпуск | Кандидат поставляется накопительным патчем. Профессиональный комплект включает новые задания и пустой протокол. | Windows 10 и 11, MSVC 2022 и Qt 6.8.3, системные 150%, Portable ZIP и Inno Setup. Существующая задача CI рассчитана на Windows Server 2022. Специалист GRAFIS 26, печать, макеты и решение о приёмке ожидаются. |

## Обновление на базе 2796753

Добавлены числовые функции формул, десять графических технологических знаков, отрицательная компенсация растяжения ткани и предпросмотр библиотечного файла по размерам. Проверка каталога знаков ещё требует привязки к контуру, переноса на припуск и семантического обмена. Для библиотеки открыты карточки и синхронизация экземпляров. Для компенсации открыты отдельные режимы детали и буфера. Открытые позиции предыдущей матрицы по размерным таблицам, градации, моделированию, производственным заказам и приёмке сохраняются. [Символы GRAFIS](https://help.grafis.de/de-de/Content/Verarbeitung/Symbole.htm) · [Режимы усадки](https://help.grafis.de/en-gb/Content/Schnittbildlegen2/Einlaufen.htm)

## Продолжение от 26 ноября 2026 года

База `9fda412eb87622cabb15d29bc2456598a3e6547a` уже содержит an-18. Готовность выпуска оценивается по отдельному протоколу.

| Направление | Добавлено | Открыто |
| --- | --- | --- |
| Размерные данные | В проекте и TSV сохраняется необязательная атрибуция размера. Фигуры таблицы 3 ГОСТ 31396-2009 помечаются таблицей и группой. | Полные первичные ряды, их редакции и поправки, правила распространения и сверка всех мерок. |
| Формулы | Условие `iflt` для формул геометрии и ширины моделирования. Вычисляется выбранная ветвь. | Общий редактор геометрических ограничений, формульные касательные и операции сложного моделирования. |
| Градация | Команда смещения кривой или детали создаёт сохраняемые правила для её базовых точек. Общий узел перемещается у обеих прилегающих кромок. | Правила, привязанные к процедурам и обновляемые при изменении топологии, и промышленная проверка размеров. |
| Производство | Для многоматериального экспорта задаются общий предел длины стола и предельное число слоёв на настил. | Сохраняемый заказ, распределение размеров и автоматический план настилов, профиль оборудования. |
| Ассистент, библиотека и интерфейс | Ранее проверенные сценарии остаются в регрессии. | Пошаговый мастер, процедурные и межпроектные связи, семантические обозначения, формы техпакета, коллекции, обновление экземпляров и операторская проверка. |

## Состояние приёмки от 27 ноября 2026 года

| Направление | Поддержка | Открыто |
| --- | --- | --- |
| Синхронизация конструкций | Несколько непересекающихся замен цепочек за одно атомарное обновление. Для переменных стыков шов пересчитывается в размере, привязка раппорта переносится по геометрии размера размещения. | Перестроение зависимых процедур с уже сгенерированными рёбрами и перенос сохранённых надсечек и знаков при меняющейся относительной позиции стыка. |
| Моделирование | Формульная прямая или фигурная стойка для нескольких деталей со своими пришивными участками и парными надсечками. У фигурной стойки стыки смещаются по длинам швов в каждом размере и сохраняют общую касательную. | Сложные припуски, универсальные ограничения, проймы с ручной градацией, двухшовные рукава, произвольные воротники и карманы и криволинейные выступы складок. |
| Промышленный заказ | Для тиража до 24 комплектов и четырёх размеров есть точный минимум числа настилов при заданных пределах слоя и комплектации. При равном числе настилов сравнивается число слоёв. | Совместная оптимизация расхода по всему заказу, материалам, геометрии, дефектам и оборудованию. |
| Размерные признаки | Для части фигур мальчиков можно добавить сверенные признаки 2, 4 и 7 ГОСТ 17917-86. Пропущенные значения не импортируются. | Остальные признаки мальчиков и дополнительные ряды девочек, женщин и мужчин. [Текст ГОСТ 17917-86 с изменениями](https://rags.ru/documents/prod/gost_gosudarstvennyj-standart/45/gost_14197.html) · [Вторая публикация](https://base.garant.ru/5904020/). |

### Дополнительная проверка от 27 ноября 2026 года

| Направление | Добавлено | Открыто |
| --- | --- | --- |
| Синхронизация | При пересчёте сгенерированных рёбер сохраняются слой DXF, надсечки и прежние идентификаторы, в том числе после переименования исходного ребра при сохранённой связности. | Смена числа сгенерированных рёбер, обновление зависимых процедур при изменении их топологии и перенос сохранённых знаков при переменном стыке. |
| Моделирование | Прямые выступы складок на деталях с квадратичной границей строятся по касательной у разреза. Исходные дуги остаются квадратичными в каждом размере. | Криволинейный профиль выступа и остальные сложные операции из основной матрицы, включая двухшовные рукава и произвольные воротники и карманы. |
| Настилы | Для одного материала оптимизируется суммарный расход по рассчитанным раскладкам всего заказа. Учитываются ширина, дефекты ткани, длина стола и число слоёв. Предел поиска — до 24 комплектов, четырёх размеров и шести деталей на слой. | Совместная оптимизация нескольких материалов и ограничений оборудования. Геометрический поиск использует существующий алгоритм раскладки и не доказывает абсолютный минимум длины каждой раскладки. |
| Размерные признаки | 877 значений признаков 2, 4 и 7 из таблицы 5 ГОСТ 17916-86 для 298 типовых фигур девочек. Импортируются только ячейки, совпавшие в двух публикациях, с ключом из таблицы 1. | Остальные дополнительные детские мерки и дополнительные женские и мужские числовые таблицы. [ГОСТ 17916-86 в Гаранте](https://base.garant.ru/5904019/) · [Публикация RAGS](https://rags.ru/documents/prod/gost_gosudarstvennyj-standart/45/gost_45585.html). |

### Проверка от 28 ноября 2026 года

| Направление | Добавлено | Открыто |
| --- | --- | --- |
| Переменные стыки | При разделении и слиянии видимых контурных рёбер переносятся надсечки, знаки и точки раппорта для размеров, присутствующих в проекте. Положение на новом ребре сохраняется с проектом. | Для размеров, добавленных после обновления, используются базовые знаки. Смена топологии сгенерированного контура и перенос зависимых процедур с изменившимся набором выходных рёбер требуют отдельной реализации. |
| Раскрой заказа | Назначение материала по деталям и расчёт размера настилов по ширине, карте дефектов и общему профилю оборудования. Для каждого материала используется свой план, а целевая функция складывает расход материалов. | Оптимизация среди нескольких станков, общий календарь и смены рулонов. Геометрическая раскладка эвристическая. Поиск ограничен 24 комплектами, четырьмя размерами и шестью деталями на слой для материала. |
| Детские мерки | Дополнительно сверены 24 772 значения 82 номерных признаков для 314 фигур мальчиков и 20 113 значений 81 признака для 298 фигур девочек. Импортируются совпавшие положительные ячейки двух публикаций. Колонки сопоставлялись по росту, обхватам груди и талии. | Пропуски и несовпадения, а также взрослые ряды за пределами встроенных ведущих признаков. [ГОСТ 17916-86, Росстандарт](https://protect.gost.ru/gost/details/06788891-adac-49c3-b3e1-168ab7c4fb06) · [ГОСТ 17916-86, Гарант](https://base.garant.ru/5904019/) · [ГОСТ 17916-86, RAGS](https://rags.ru/documents/prod/gost_gosudarstvennyj-standart/45/gost_45585.html) · [ГОСТ 17917-86, Гарант](https://base.garant.ru/5904020/) · [ГОСТ 17917-86, RAGS](https://rags.ru/documents/prod/gost_gosudarstvennyj-standart/45/gost_14197.html) |
