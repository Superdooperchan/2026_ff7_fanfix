// Generic 2D 
Texture2D tex0 : register(t0);

SamplerState PointSampler 
{
	Filter = MIN_MAG_MIP_POINT; AddressU = Clamp; AddressV = Clamp;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 uv0 : TEXCOORD0;
    //HQ4x had a bunch of these so I added em just to be safe. May be un-needed, can prob remove.
	float4 uv1 : TEXCOORD1;
    float4 uv2 : TEXCOORD2;
    float4 uv3 : TEXCOORD3;
    float4 uv4 : TEXCOORD4;
    float4 uv5 : TEXCOORD5;
    float4 uv6 : TEXCOORD6;
    float4 uv7 : TEXCOORD7;
    float4 color : COLOR0;
};

float4 main(PS_INPUT input) : SV_Target
{
    return tex0.Sample(PointSampler, input.uv0.xy);
}