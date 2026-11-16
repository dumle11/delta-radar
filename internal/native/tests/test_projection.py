"""Check the production projection with known camera-space coordinates."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "esp_overlay.h").read_text()
start = source.index("static int _ov_w2s(")
end = source.index("\n}", start) + 2
program = "#include <math.h>\n#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\nstatic int _ov_w, _ov_h;\nstatic float _ov_cam_roll;\n#include \"render_projection.h\"\n"
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
    assert(_ov_w2s(10,1,0,0,0,0,0,0,138,&x,&y));
    assert(fabsf((x-960)-960/tanf(69*M_PI/180)/10)<.01f); // Active override changes focal scale.
    float down=30*(float)(M_PI/180);
    assert(_ov_w2s(10*cosf(down),0,-10*sinf(down),0,0,0,0,-30,138,&x,&y));
    assert(fabsf(x-960)<.01 && fabsf(y-540)<.01); // Looking down keeps the forward ray centered.
    _ov_cam_roll=90;
    assert(_ov_w2s(10,1,0,0,0,0,0,0,90,&x,&y));
    assert(fabsf(x-960)<.01 && fabsf(y-444)<.01);
    _ov_cam_roll=0;
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
    RPData data={.origin={100,200,300},.rotation={0,0,1,0, 1,0,0,0, 0,1,0,0, 0,0,0,1},
        .projection={.5625f,0,0,0, 0,1,0,0, 0,0,0,1, 0,0,10,0},
        .view_rect={0,0,1920,1080},.rect={0,0,1920,1080}};
    assert(rp_valid_data(&data));rp_frame.data=data;rp_frame.valid=1;rp_frame.scale_x=rp_frame.scale_y=1;
    assert(_ov_w2s(11,3,3,999,999,999,120,50,55,&x,&y));
    assert(fabsf(x-1014)<.01 && fabsf(y-540)<.01); // Native projection wins over guessed FOV/camera axes.
    assert(_ov_w2s(11,2,4,0,0,0,0,0,90,&x,&y) && fabsf(y-486)<.01);
    assert(!_ov_w2s(0,2,3,0,0,0,0,0,90,&x,&y));
    rp_frame.data.rect[0]=100;rp_frame.data.rect[1]=200;rp_frame.data.rect[2]=1820;rp_frame.data.rect[3]=880;
    assert(_ov_w2s(11,2,3,0,0,0,0,0,90,&x,&y) && fabsf(x-960)<.01 && fabsf(y-540)<.01);
    data.projection[0]=NAN;assert(!rp_valid_data(&data));
    return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    cfile = Path(directory) / "projection.c"
    executable = Path(directory) / "projection"
    cfile.write_text(program)
    subprocess.run(["cc", "-O2", str(cfile), "-o", str(executable), "-I", str(Path(__file__).resolve().parents[1]), "-lm"], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: projection scale, viewport orientation, rotation, offscreen endpoints, invalid inputs")
