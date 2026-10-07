# HOME sorter development fork

This branch extends upstream Pokemon Automation with verified HOME navigation,
resumable inventory indexing, editable collection metadata and saved sorting
plans. Controller support follows upstream: ESP32-S3/PABotBase2 is supported;
the temporary Teensy/legacy PABotBase compatibility patch is not included.

## Build and run

Use the upstream [build instructions](SerialPrograms/README.md) and the normal
Packages resources/firmware. CMake also deploys this branch's pinned
HomeCollectionReference.json into Resources/Pokemon automatically. Follow the
[Collection Manager instructions](SerialPrograms/Scripts/home_collection/README.md)
for indexing, offline plan preparation and native execution/resume. Keep exports,
checkpoints and logs outside source control.

The sorter supports occupied-slot swaps, so a completely full HOME needs no
empty staging slot for species ordering. All copies are retained; release is
unavailable. Unknown forms/IVs remain unknown. Boxes outside the indexed physical
range are never plan destinations. Leave the planned range unchanged while paused.

## Tests

From the repository root:

```sh
python3 -m unittest discover -s SerialPrograms/Scripts/home_collection -v
python3 -m unittest discover -s SerialPrograms/Scripts/box_sorter -v
```

Native executor tests on Windows:

```powershell
./SerialPrograms/Scripts/box_sorter/test_saved_plan_execution.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64'
```

Qt-free policies also compile with g++ on Linux. Their sources are in
SerialPrograms/Scripts/box_sorter. The checkpoint/executor harnesses need QtCore.
See [navigation timing](SerialPrograms/Scripts/box_sorter/TIMING.md) for the
separation between offline tests, modeled route costs and physical validation.

## Moving to Linux

The source is cross-platform, but a Windows build cannot be reused on Linux.
Build the GUI with the compatible Qt modules and upstream C++ dependencies,
configure access to the USB serial adapter/capture card, and choose the Linux
serial device in the app. Do not use the Qt-free core build as a substitute for
the HOME vision sorter. The upstream Ubuntu guide reports capture flickering;
validate actual video and controller behavior on native Linux before a long job.
WSL policy tests do not establish native Linux capture compatibility.

Before migration, stop unheld at a committed checkpoint and copy the exact plan,
checkpoint and inventory/workspace exports to the new machine. If an operation
was interrupted, retain its in-flight checkpoint. Source fingerprints depend on
physical range/observations, not filenames or Windows path prefixes. Preserve
relative resource layout and verify a bounded operation before a full run.

## Current limits

This is development source, not a packaged alpha release. Full long-run throughput
still needs live validation. Plan preparation currently uses the offline Python
CLI; the GUI runs/resumes plans without an external runner. Advanced exact-form
layouts, quality ranking, deletion, controller passthrough and headless operation
are separate future work.
