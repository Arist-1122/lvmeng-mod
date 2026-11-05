// Generated from exact serialized Light RGB edits. Alpha/strength is untouched.
float4 main(float4 value:TEXCOORD0):SV_Target {
    float peak=max(value.r,max(value.g,value.b));
    if (!(peak>=0.00001 && peak<=65504.0 && all(value.rgb>=0))) return value;
    float3 hue=value.rgb/peak;
    if (all(abs(hue-float3(0.3160377145,0.6787449718,1))<=0.00002)) return float4(peak*float3(1,0.06528301537,0.2202599049), value.a);
    if (all(abs(hue-float3(0.08143243693,0.4183140276,1))<=0.00002)) return float4(peak*float3(1,0.005453614471,0.03977197795), value.a);
    if (all(abs(hue-float3(0.7159999722,0.8273724883,1))<=0.00002)) return float4(peak*float3(1,0.09727999802,0.2358171498), value.a);
    if (all(abs(hue-float3(0.4852226065,0.6615847947,1))<=0.00002)) return float4(peak*float3(1,0.01485244718,0.05559548153), value.a);
    if (all(abs(hue-float3(0.5707547069,0.7888957858,1))<=0.00002)) return float4(peak*float3(1,0.08566037565,0.2354539782), value.a);
    if (all(abs(hue-float3(0.285472315,0.5851770166,1))<=0.00002)) return float4(peak*float3(1,0.007939741297,0.04524594745), value.a);
    return value;
}
