#pragma once
struct hair_program_asset {uint32_t stock_bytes,stock_crc,candidate_crc; const char *stock_sha; owned_file candidate;};
static constexpr uint32_t hair_vs_size=17344,hair_vs_crc=0x19CD9126;
static constexpr char hair_vs_sha[]="d72f38d317c566f37fbe11bf5fd73612f38b231a6930e520a1c0109d6336113d";
static constexpr hair_program_asset hair_programs[]={
 {23204,0x9C67CE28,0x3849B2D0,"5def47face873649b550538fb4821026300bdb7a38529c90898db438fbaadcd1",{L"payload/HairLight_9C67CE28.dxbc",24444,"7a17e8ee0f95e9f1b62ae38f0e8911ada00519a917dee843d376f16d63fb1886"}},
 {25656,0x90E3C4A4,0xA1070C78,"edf5ca3eb3060e331c4fcbe8e2469ccb43423264510465686d5ec8e9566091c1",{L"payload/HairLight_90E3C4A4.dxbc",26896,"0fba8cab6da04473cd8af86f276b9bd359a5c907e7ea86c5e3f57956e787d84c"}},
 {25568,0x7095AA0C,0x90C79FC8,"b8d0f5ce40a5480a77ced6a3ca16c7acdd247cdf31debe24504da953049a2233",{L"payload/HairLight_7095AA0C.dxbc",26808,"1fece21bd24b25a779a7f5345efc687b5faa9a29587c806d751c20e5e2518341"}},
};
