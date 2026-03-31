#version 450

//////////////////////////////////////////////////////////////////
// parameters

#pragma parameter SwapEyes      "Swap Eyes"                  0.0 0.0 1.0 1.0
#pragma parameter OutputMode    "Output Mode (0=SBS 1=TaB)" 0.0 0.0 1.0 1.0

layout(push_constant) uniform Push
{
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;

    float SwapEyes;
    float OutputMode;
} params;

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
} global;

//////////////////////////////////////////////////////////////////
// vertex shader

#pragma stage vertex

layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

//////////////////////////////////////////////////////////////////
// fragment shader

#pragma stage fragment

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

vec4 sampleSource(int x, int y)
{
    x = clamp(x, 0, int(params.SourceSize.x) - 1);
    y = clamp(y, 0, int(params.SourceSize.y) - 1);
    return texelFetch(Source, ivec2(x, y), 0);
}

void main()
{
    vec2 uv = vTexCoord;

    bool isTaB = (params.OutputMode > 0.5);

    bool secondEye;
    float eyeU;
    float eyeV;

    //////////////////////////////////////////////////////////////
    // Output layout mapping
    //////////////////////////////////////////////////////////////

    if (!isTaB)
    {
        // SBS output
        bool rightHalf = (uv.x >= 0.5);
        secondEye = rightHalf;

        eyeU = rightHalf ? ((uv.x - 0.5) * 2.0) : (uv.x * 2.0);
        eyeV = uv.y;
    }
    else
    {
        // TaB / Over-Under output
        bool bottomHalf = (uv.y >= 0.5);
        secondEye = bottomHalf;

        eyeU = uv.x;
        eyeV = bottomHalf ? ((uv.y - 0.5) * 2.0) : (uv.y * 2.0);
    }

    bool outputIsLeftEye = !secondEye;

    bool leftEyeOnEvenRows = (params.SwapEyes > 0.5);

    float srcW = params.SourceSize.x;
    float srcH = params.SourceSize.y;

    int srcX = clamp(int(eyeU * srcW), 0, int(srcW) - 1);

    //////////////////////////////////////////////////////////////
    // Vertical interpolation reconstruction
    //////////////////////////////////////////////////////////////

    float y = eyeV * srcH;

    // Each eye occupies every other row
    float eyeRowF = y * 0.5;

    int eyeRow0 = int(floor(eyeRowF));
    int eyeRow1 = eyeRow0 + 1;

    float t = fract(eyeRowF);

    int phase = (outputIsLeftEye == leftEyeOnEvenRows) ? 0 : 1;

    int srcY0 = eyeRow0 * 2 + phase;
    int srcY1 = eyeRow1 * 2 + phase;

    vec4 c0 = sampleSource(srcX, srcY0);
    vec4 c1 = sampleSource(srcX, srcY1);

    FragColor = mix(c0, c1, t);
}
