# Runtime regression checks

`compat_smoke.py` is a UART7 REPL regression script for the legacy QuecPython
module-import and API matrix. Copy it to `/usr` or paste it in the REPL and run
`import compat_smoke; compat_smoke.run()`.

The script is intentionally not a frozen module: release firmware carries only
the runtime modules that product code needs.
