"""Check the production projection with known camera-space coordinates."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "esp_overlay.h").read_text()
start = source.index("static int _ov_w2s(")
end = source.index("\n}", start) + 2
program = "#include <math.h>\n#include <assert.h>\nstatic int _ov_w, _ov_h;\n"
program += source[start:end]
program += r"""
int main(void) {
    float x, y, right, up;
    _ov_w = 1920; _ov_h = 1080;
    assert(_ov_w2s(10,0,0, 0,0,0, 0,0,90, &x,&y));
    assert(fabsf(x-960)<0.01f && fabsf(y-540)<0.01f);
    assert(_ov_w2s(10,1,0, 0,0,0, 0,0,90, &x,&y)); right=x-960;
    assert(_ov_w2s(10,0,1, 0,0,0, 0,0,90, &x,&y)); up=540-y;
    assert(fabsf(right-up)<0.01f && fabsf(right-96)<0.01f);
    /* A head outside the viewport must still produce geometry for clipping. */
    assert(_ov_w2s(10,0,20, 0,0,0, 0,0,90, &x,&y));
    assert(y<0);
    assert(!_ov_w2s(-10,0,0, 0,0,0, 0,0,90, &x,&y));
    assert(!_ov_w2s(NAN,0,0, 0,0,0, 0,0,90, &x,&y));
    _ov_w=1080; _ov_h=1920;
    assert(_ov_w2s(10,0,0, 0,0,0, 0,0,90, &x,&y));
    assert(fabsf(x-540)<0.01f && fabsf(y-960)<0.01f);
    assert(_ov_w2s(0,10,0, 0,0,0, 90,0,90, &x,&y));
    assert(fabsf(x-540)<0.01f && fabsf(y-960)<0.01f);
    return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    cfile = Path(directory) / "projection.c"
    executable = Path(directory) / "projection"
    cfile.write_text(program)
    subprocess.run(["cc", "-O2", str(cfile), "-o", str(executable), "-lm"], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: projection scale, viewport orientation, rotation, offscreen endpoints, invalid inputs")
