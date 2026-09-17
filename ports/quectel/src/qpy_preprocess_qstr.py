import re
import subprocess
import sys


def main():
    if len(sys.argv) < 5 or "--" not in sys.argv:
        print("usage: qpy_preprocess_qstr.py output compiler flags... -- inputs...")
        return 2

    sep = sys.argv.index("--")
    output = sys.argv[1]
    compiler = sys.argv[2]
    flags = sys.argv[3:sep]
    inputs = sys.argv[sep + 1 :]

    qstr_line = re.compile(r"^Q\(.*\)")
    data = []
    for path in inputs:
        with open(path, "r", encoding="utf-8") as infile:
            for line in infile:
                stripped = line.rstrip("\r\n")
                if qstr_line.match(stripped):
                    data.append('"%s"\n' % stripped)
                else:
                    data.append(line)

    proc = subprocess.run(
        [compiler, "-E"] + flags + ["-"],
        input="".join(data).encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        sys.stderr.buffer.write(proc.stderr)
        return proc.returncode

    out_lines = []
    qcfg_out = re.compile(r"^QCFG\(.*\)$")
    for raw_line in proc.stdout.decode("utf-8", errors="ignore").splitlines():
        line = raw_line.strip()
        if line.startswith('"Q(') and line.endswith(')"'):
            out_lines.append(line[1:-1])
        elif qcfg_out.match(line):
            out_lines.append(line)

    with open(output, "w", encoding="utf-8", newline="\n") as outfile:
        outfile.write("\n".join(out_lines))
        outfile.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
