#include "GPUEngine.h"

using namespace std;


void GPUEngine::GenerateCode(Secp256K1 &secp, int size) {

  // Compute generator table
  Point *Gn = new Point[size];
  Point g = secp.G;
  Gn[0] = g;
  g = secp.DoubleDirect(g);
  Gn[1] = g;
  for (int i = 2; i < size; i++) {
    g = secp.AddDirect(g, secp.G);
    Gn[i] = g;
  }

  int log2n = (int)(log2(size) + 0.5);

  // Write file
  FILE *f = fopen("GPUGroup.h", "w");

  fprintf(f, "// File generated by GPUEngine::GenerateCode()\n");
  fprintf(f, "// GROUP definitions\n");
  fprintf(f, "#define GRP_SIZE %d\n", size);
  fprintf(f, "#define GRP_SIZE_LOG2 %d\n\n", log2n);
  fprintf(f, "// SecpK1 Generator table (Contains G,2G,3G,...)\n");
  fprintf(f, "__device__ __constant__ uint64_t Gx[][4] = {\n");
  for (int i = 0; i < size; i++) {
    fprintf(f, "  %s,\n", Gn[i].x.GetC64Str(4).c_str());
  }
  fprintf(f, "};\n");

  fprintf(f, "__device__ __constant__ uint64_t Gy[][4] = {\n");
  for (int i = 0; i < size; i++) {
    fprintf(f, "  %s,\n", Gn[i].y.GetC64Str(4).c_str());
  }
  fprintf(f, "};\n\n");

  fclose(f);
  delete[] Gn;

}
