# Source provenance

The component source is extracted directly from the user-designated V1.1 UniRTOS adaptation snapshot. Directory layout was changed for external-library packaging, but port source files, the MicroPython tree, LittleFS, frozen modules, and EG800ZCN_LA board configuration were copied from that snapshot without source-level redesign.

Generated output, legacy platform SDK content, logs, batch wrappers, Python bytecode/cache directories, and machine-specific absolute paths are intentionally excluded. The board-specific `customer_app2.bin` is a required 288 KiB first-boot LittleFS release input copied byte-for-byte from the snapshot's `tools/customer_fs.bin`; it is not a retained build output. Its size and SHA-256 are recorded in `boards/EG800ZCN_LA/customer_app2.sha256`.

Repository source is stored as readable Git content. Any local source-protection view is a developer-workstation concern and is not a property of the repository or a release requirement.

The port file `ports/quectel/src/qpy_usrfs.c` carries one added guard include (`boards/EG800ZCN_LA/qpy_partition_layout.h`), which asserts that the SDK partition macros match the EG800ZCN_LA CUST layout required by the packaged image. The header applies that override only in QuecPython compilation units; the component never rewrites SDK files.
