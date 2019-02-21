#include "Vanity.h"
#include "Base58.h"
#include "hash/sha256.h"
#include "hash/sha512.h"
#include "IntGroup.h"
#include "Timer.h"

using namespace std;

DWORD WINAPI _FindKey(LPVOID lpParam);

Point Gn[CPU_GRP_SIZE];

// ----------------------------------------------------------------------------

VanitySearch::VanitySearch(Secp256K1 &secp,string prefix,string seed,bool comp, bool useGpu, int gpuId, bool stop, int gridSize) {

  this->vanityPrefix = prefix;
  this->secp = secp;
  this->searchComp = comp;
  this->useGpu = useGpu;
  this->gpuId = gpuId;
  this->stopWhenFound = stop;
  this->gridSize = gridSize;
  sPrefix = -1;
  std::vector<unsigned char> result;

  if (prefix.length() < 2) {
    printf("VanitySearch: Invalid prefix !");
    exit(-1);
  }

  if (prefix.data()[0] != '1' ) {
    printf("VanitySearch: Only prefix starting with 1 allowed !");
    exit(-1);
  }

  string dummy1 = prefix;
  int nbDigit = 0;
  bool wrong = false;

  char ctimeBuff[64];
  time_t now = time(NULL);
  ctime_s((char *const)&ctimeBuff, 64, &now);
  printf("Start %s", ctimeBuff);

  printf("Search: %s\n",dummy1.c_str());

  // Search for highest hash160 16bit prefix (most probable)

  while (result.size() < 25 && !wrong) {
    wrong = !DecodeBase58(dummy1, result);
    if (result.size() < 25) {
      dummy1.append("1");
      nbDigit++;
    }
  }

  if (wrong) {
    printf("VanitySearch: Wrong character 0, I, O and l not allowed !");
    exit(-1);
  }

  if (result.size() != 25) {
    printf("VanitySearch: Wrong prefix !");
    exit(-1);
  }

  //printf("VanitySearch: Found prefix %s\n",GetHex(result).c_str() );
  sPrefix = *(prefix_t *)(result.data()+1);

  dummy1.append("1");
  DecodeBase58(dummy1, result);

  if (result.size() == 25) {
    //printf("VanitySearch: Found prefix %s\n", GetHex(result).c_str());
    sPrefix = *(prefix_t *)(result.data()+1);
    nbDigit++;
  }
  
  // Difficulty
  _difficulty = pow(2,192) / pow(58,nbDigit);
  printf("Difficulty: %.0f\n", _difficulty);

  // Compute Generator table G[n] = (n+1)*G

  Point g = secp.G;
  Gn[0] = g;
  g = secp.DoubleDirect(g);
  Gn[1] = g;
  for (int i = 2; i < CPU_GRP_SIZE; i++) {
    g = secp.AddDirect(g,secp.G);
    Gn[i] = g;
  }

  // Seed
  if (seed.length() == 0) {
    // Default seed
    seed = to_string(Timer::qwTicksPerSec.LowPart) + to_string(Timer::perfTickStart.HighPart) +
           to_string(Timer::perfTickStart.LowPart) + to_string(time(NULL));
  }

  // Protect seed against "seed search attack" using pbkdf2_hmac_sha512
  string salt = "VanitySearch";
  unsigned char hseed[64];
  pbkdf2_hmac_sha512(hseed, 64, (const uint8_t *)seed.c_str(), seed.length(),
    (const uint8_t *)salt.c_str(), salt.length(),
    2048);
  startKey.SetInt32(0);
  sha256(hseed, 64, (unsigned char *)startKey.bits64);

  printf("Base Key:%s\n",startKey.GetBase16().c_str());

}

// ----------------------------------------------------------------------------

string VanitySearch::GetExpectedTime(double keyRate,double keyCount) {

  char tmp[128];
  string ret;

  double P = 1.0/_difficulty;
  // pow(1-P,keyCount) is the probality of failure after keyCount tries
  double cP = 1.0 - pow(1-P,keyCount);

  sprintf_s(tmp,"[P %.2f%%]",cP*100.0);
  ret = string(tmp);
  
  double desiredP = 0.5;
  while(desiredP<cP)
    desiredP += 0.1;
  if(desiredP>=0.99) desiredP = 0.99;

  double k = log(1.0-desiredP)/log(1.0-P);

  int64_t dTime = (int64_t)((k-keyCount)/keyRate); // Time to perform k tries

  if(dTime<0) dTime = 0;

  double dP = 1.0 - pow(1 - P, k);

  int nbDay  = (int)(dTime / 86400 );
  if (nbDay >= 1) {

    sprintf_s(tmp, "[%.2f%% in %.1fd]", dP*100.0, (double)dTime / 86400);

  } else {

    int nbHour = (int)((dTime % 86400) / 3600);
    int nbMin = (int)(((dTime % 86400) % 3600) / 60);
    int nbSec = (int)(dTime % 60);

    sprintf_s(tmp, "[%.2f%% in %02d:%02d:%02d]", dP*100.0, nbHour, nbMin, nbSec);

  }



  return ret + string(tmp);

}

// ----------------------------------------------------------------------------

bool VanitySearch::checkAddr(string &addr, Int &key, uint64_t incr) {

  char p[64];
  char a[64];

  strcpy(p,vanityPrefix.c_str());
  strcpy(a,addr.c_str());
  a[vanityPrefix.length()] = 0;

  if (strcmp(p, a) == 0) {
    Int k(&key);
    k.Add(incr);
    // Found it
    printf("\nFound address:\n");
    printf("Pub Addr: %s\n", addr.c_str());
    printf("Prv Addr: %s\n", secp.GetPrivAddress(k).c_str());
    printf("Prv Key : 0x%s\n", k.GetBase16().c_str());
    // Check
    Point p = secp.ComputePublicKey(&k);
    printf("Check   : %s\n", secp.GetAddress(p,false).c_str());
    printf("Check   : %s (comp)\n", secp.GetAddress(p, true).c_str());
    return true;
  }

  /*
  if (stricmp(p,a) == 0) {
    // Found it (case unsensitive)
    printf("\nFound address:\n");
    printf("Pub Addr: %s\n", addr.c_str());
    printf("Prv Addr: %s\n", secp.GetPrivAddress(key).c_str());
    printf("Prv Key : 0x%s\n", key.GetBase16().c_str());
    //Point p = secp.ComputePublicKey(&key);
    //printf("Check :%s\n", secp.GetAddress(p,true).c_str());
    return true;
  }
  */

  return false;
}

// ----------------------------------------------------------------------------

DWORD WINAPI _FindKey(LPVOID lpParam) {
  TH_PARAM *p = (TH_PARAM *)lpParam;
  p->obj->FindKeyCPU(p);
  return 0;
}

DWORD WINAPI _FindKeyGPU(LPVOID lpParam) {
  ((VanitySearch *)lpParam)->FindKeyGPU();
  return 0;
}

// ----------------------------------------------------------------------------

void VanitySearch::FindKeyCPU(TH_PARAM *ph) {

  unsigned char h0[20];
  unsigned char h1[20];
  unsigned char h2[20];
  unsigned char h3[20];

  // Global init
  int thId = ph->threadId;
  counters[thId] = 0;

  // CPU Thread
  IntGroup *grp = new IntGroup();

  // Group Init
  Int key(&startKey);
  Int off(0LL);
  off.Add((uint64_t)thId);
  off.ShiftL(64);
  key.Add(&off);
  Point startP = secp.ComputePublicKey(&key);

  Int dx[CPU_GRP_SIZE];
  Point pts[CPU_GRP_SIZE];
  Int dy;
  Int _s;
  Int _p;
  Point p = startP;
  grp->Set(dx);

  while (!endOfSearch) {

    // Fill group

    for (int i = 0; i < CPU_GRP_SIZE; i++) {
      dx[i].ModSub(&Gn[i].x, &startP.x);
    }

    // Grouped ModInv
    grp->ModInv();

    for (int i = 0; i < CPU_GRP_SIZE; i++) {

      pts[i] = p;
      p = startP;

      dy.ModSub(&Gn[i].y, &p.y);

      _s.MontgomeryMult(&dy, &dx[i]);     // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
      _p.MontgomeryMult(&_s, &_s);        // _p = pow2(s)*R^-3
      _p.MontgomeryMult(Int::GetR4());    // _p = pow2(s)

      p.x.ModNeg();
      p.x.ModAdd(&_p);
      p.x.ModSub(&Gn[i].x);               // rx = pow2(s) - p1.x - p2.x;

      p.y.ModSub(&Gn[i].x, &p.x);
      p.y.MontgomeryMult(&_s);
      p.y.MontgomeryMult(Int::GetR3());
      p.y.ModSub(&Gn[i].y);               // ry = - p2.y - s*(ret.x-p2.x);  

    }

    // Check addresses (compressed)

    for (int i = 0; i < CPU_GRP_SIZE; i += 4) {

      secp.GetHash160(searchComp, pts[i], pts[i+1], pts[i+2], pts[i+3], h0, h1, h2, h3);

      prefix_t pr0 = *(prefix_t *)h0;
      prefix_t pr1 = *(prefix_t *)h1;
      prefix_t pr2 = *(prefix_t *)h2;
      prefix_t pr3 = *(prefix_t *)h3;

      if (pr0 == sPrefix) {
        string addr = secp.GetAddress(pts[i], searchComp);
        endOfSearch = checkAddr(addr, key, i) && stopWhenFound;
      }
      if (pr1 == sPrefix) {
        string addr = secp.GetAddress(pts[i+1], searchComp);
        endOfSearch = checkAddr(addr, key, i + 1) && stopWhenFound;
      }
      if (pr2 == sPrefix) {
        string addr = secp.GetAddress(pts[i+2], searchComp);
        endOfSearch = checkAddr(addr, key, i + 2) && stopWhenFound;
      }
      if (pr3 == sPrefix) {
        string addr = secp.GetAddress(pts[i+3], searchComp);
        endOfSearch = checkAddr(addr, key, i + 3) && stopWhenFound;
      }

    }

    key.Add((uint64_t)CPU_GRP_SIZE);
    startP = p;
    counters[thId]+= CPU_GRP_SIZE;

  }


}

// ----------------------------------------------------------------------------

void VanitySearch::FindKeyGPU() {

  bool ok = true;

  // Global init
  GPUEngine g(gridSize, gpuId);
  int nbThread = g.GetNbThread();
  Point *p = new Point[nbThread];
  Int *keys = new Int[nbThread];
  vector<ITEM> found;

  printf("GPU: %s\n",g.deviceName.c_str());

  counters[0xFF] = 0;

  for (int i = 0; i < nbThread; i++) {
    keys[i].Set(&startKey);
    Int off((uint64_t)i);
    off.ShiftL(96);
    keys[i].Add(&off);
    p[i] = secp.ComputePublicKey(&keys[i]);
  }
  g.SetSearchMode(searchComp);
  g.SetPrefix(sPrefix);
  ok = g.SetKeys(p);

  // GPU Thread
  while (ok && !endOfSearch) {

    // Call kernel
    ok = g.Launch(found);

    for(int i=0;i<(int)found.size() && !endOfSearch;i++) {

      ITEM it = found[i];
      string addr = secp.GetAddress(it.hash, searchComp);
      endOfSearch = checkAddr(addr, keys[it.thId], it.incr) && stopWhenFound;
 
    }

    if (ok) {
      for (int i = 0; i < nbThread; i++) {
        keys[i].Add((uint64_t)STEP_SIZE);
      }
      counters[0xFF] += STEP_SIZE * nbThread;
    }

  }

  // GPU thread may exit on error
  if(nbCpuThread==0)
    endOfSearch = true;

  delete[] keys;
  delete[] p;

}

// ----------------------------------------------------------------------------

void VanitySearch::Search(int nbThread) {

  double t0;
  double t1;
  endOfSearch = false;
  nbCpuThread = nbThread;

  memset(counters,0,sizeof(counters));

  ghMutex = CreateMutex(NULL, FALSE, NULL);

  printf("Number of CPU thread: %d\n", nbThread);

  TH_PARAM *params = (TH_PARAM *)malloc((nbThread + (useGpu?1:0)) * sizeof(TH_PARAM));

  for (int i = 0; i < nbThread; i++) {
    DWORD thread_id;
    params[i].obj = this;
    params[i].threadId = i;
    CreateThread(NULL, 0, _FindKey, (void*)(params+i), 0, &thread_id);
  }

  if (useGpu) {
    DWORD thread_id;
    params[nbThread].obj = this;
    params[nbThread].threadId = 255;
    CreateThread(NULL, 0, _FindKeyGPU, (void*)(this), 0, &thread_id);
  }

  t0 = Timer::get_tick();
  startTime = t0;
  __int64 lastCount = 0;
  __int64 lastGPUCount = 0;
  while (!endOfSearch) {

    int delay = 2000;
    while (!endOfSearch && delay>0) {
      Sleep(500);
      delay -= 500;
    }

    __int64 count = 0;
    for (int i = 0; i < nbThread; i++)
      count += counters[i];
    if(useGpu)
      count += counters[0xFF];

    t1 = Timer::get_tick();
    double keyRate = (double)(count - lastCount) / (t1 - t0);
    double gpuKeyRate = (double)(counters[0xFF] - lastGPUCount) / (t1 - t0);

    if (!endOfSearch) {
      printf("%.3f MK/s (GPU %.3f MK/s) (2^%.2f) %s\r",
      keyRate / 1000000.0, gpuKeyRate / 1000000.0,
      log2((double)count), GetExpectedTime(keyRate, (double)count).c_str());
    }

    lastCount = count;
    lastGPUCount = counters[0xFF];
    t0 = t1;

  }

  free(params);

}

// ----------------------------------------------------------------------------

string VanitySearch::GetHex(vector<unsigned char> &buffer) {

  string ret;

  char tmp[128];
  for (int i = 0; i < (int)buffer.size(); i++) {
    sprintf_s(tmp,"%02X",buffer[i]);
    ret.append(tmp);
  }

  return ret;

}
