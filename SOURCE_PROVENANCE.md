# Source provenance

The component source is extracted directly from the user-designated V1.1 UniRTOS adaptation snapshot. Directory layout was changed for external-library packaging, but port source files, the MicroPython tree, LittleFS, frozen modules, and EG800ZCN_LA board configuration were copied from that snapshot without source-level redesign.

Generated output, legacy platform SDK content, logs, batch wrappers, Python bytecode/cache directories, and machine-specific absolute paths are intentionally excluded. The board-specific `customer_app2.bin` is a required 288 KiB release input copied byte-for-byte from the snapshot's `tools/customer_fs.bin`; it is not a retained build output.

The source snapshot contains files managed by E-SafeNet. They are retained exactly as supplied, per release authorization, and require an authorized build environment capable of exposing their source contents to CMake and the compiler. No decrypted or reconstructed substitutes are mixed into this tree.

The port file `ports/quectel/src/qpy_usrfs.c` carries one added guard include (`boards/EG800ZCN_LA/qpy_partition_layout.h`), which asserts that the SDK partition macros match the EG800ZCN_LA CUST layout required by the packaged image. This is the only source-level change introduced during component packaging.
