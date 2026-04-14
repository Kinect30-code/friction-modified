#version 330 core

in vec3 vNormal;
in vec3 vViewDir;
in vec2 vTexCoord0;
in vec2 vTexCoord1;

uniform int uRenderMode;
uniform int uHasTexture;
uniform int uAlphaMode;
uniform int uTexCoordSet;
uniform vec4 uBaseColorFactor;
uniform float uAlphaCutoff;
uniform vec2 uTexCoordOffset;
uniform vec2 uTexCoordScale;
uniform float uTexCoordRotation;
uniform sampler2D uBaseColorTexture;

out vec4 FragColor;

vec3 environmentColor(vec3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ground = vec3(0.05, 0.06, 0.08);
    vec3 horizon = vec3(0.25, 0.30, 0.36);
    vec3 zenith = vec3(0.58, 0.67, 0.82);
    return mix(mix(ground, horizon, smoothstep(0.0, 0.45, t)),
               zenith,
               smoothstep(0.45, 1.0, t));
}

vec3 shadeUnlit(vec3 n, vec3 v) {
    float wrap = n.y * 0.5 + 0.5;
    float fresnel = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 base = mix(vec3(0.66, 0.72, 0.79), environmentColor(n), 0.22);
    base += wrap * vec3(0.06, 0.08, 0.10);
    base += fresnel * vec3(0.10, 0.10, 0.12);
    return base;
}

vec3 shadeMatcap(vec3 n, vec3 v) {
    vec3 r = reflect(-v, n);
    float denom = max(2.8284271247461903 * sqrt(max(r.z + 1.0, 0.0001)), 0.0001);
    vec2 uv = r.xy / denom + 0.5;
    vec2 p = uv * 2.0 - 1.0;
    float mask = clamp(1.0 - dot(p, p), 0.0, 1.0);
    vec3 base = mix(vec3(0.08, 0.09, 0.10),
                    vec3(0.96, 0.97, 0.99),
                    pow(mask, 0.55));
    vec3 tint = vec3(0.57, 0.74, 0.94);
    return mix(base, tint, 0.18 + 0.45 * pow(mask, 2.4));
}

void main() {
    vec3 n = normalize(vNormal);
    if(length(n) < 0.0001) {
        n = vec3(0.0, 0.0, 1.0);
    }
    vec3 v = normalize(vViewDir);
    vec4 baseColor = uBaseColorFactor;
    if(uHasTexture != 0) {
        vec2 uv = uTexCoordSet == 0 ? vTexCoord0 : vTexCoord1;
        uv *= uTexCoordScale;
        float c = cos(uTexCoordRotation);
        float s = sin(uTexCoordRotation);
        uv = vec2(c * uv.x - s * uv.y,
                  s * uv.x + c * uv.y);
        uv += uTexCoordOffset;
        baseColor *= texture(uBaseColorTexture, uv);
    }

    float alpha = uAlphaMode == 0 ? 1.0 : baseColor.a;
    if(uAlphaMode == 1 && alpha < uAlphaCutoff) {
        discard;
    }

    vec3 color = uRenderMode == 0 ?
                baseColor.rgb :
                shadeMatcap(n, v) * baseColor.rgb;
    if(uRenderMode != 0) {
        float rim = pow(1.0 - max(dot(n, v), 0.0), 2.0);
        color += rim * vec3(0.10, 0.11, 0.12) * baseColor.rgb;
    }
    FragColor = vec4(color, alpha);
}
