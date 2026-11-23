#version 450
layout(set=0,binding=0) uniform sampler2D pixels;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(constant_id=0) const bool srgbTarget=false;
layout(constant_id=1) const int rotation=0;
void main() {
    vec2 sampleUV=uv;
    if(rotation==1) sampleUV=vec2(uv.y,1.0-uv.x);
    else if(rotation==2) sampleUV=vec2(1.0-uv.x,1.0-uv.y);
    else if(rotation==3) sampleUV=vec2(1.0-uv.y,uv.x);
    color=texture(pixels,sampleUV);
    // Canvas pixels use premultiplied sRGB bytes. Decode only for an sRGB target.
    if(srgbTarget && color.a>0.0) {
        vec3 c=clamp(color.rgb/color.a,0.0,1.0);
        color.rgb=mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045)))*color.a;
    }
}
