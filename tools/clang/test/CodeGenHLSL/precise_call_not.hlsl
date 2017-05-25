// RUN: %dxc -E main -T ps_6_0 %s | FileCheck %s

// CHECK: fast

float4 main(float a : A) : SV_Target
{
  float psin = sin(a);

  return psin;
}
