// vst2_render.c — headless VST2 host to render the ORIGINAL VB-1 (x86_64, under Rosetta).
// AEffect offsets from vb1.vst base ctor (0x681e): processReplacing @ +0x78 (realQualities
// is int32 here, collapsing the canonical padding). Usage:
//   arch -x86_64 ./vst2_render <vb1.vst/Contents/MacOS/vb1> <out.wav> [mains]
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

typedef int64_t  VstIntPtr;
typedef int32_t  VstInt32;
typedef float    sample_t;
typedef VstIntPtr (*audioMasterCB)(void*, VstInt32, VstInt32, VstIntPtr, void*, float);
typedef VstIntPtr (*dispatcher_t) (void*, VstInt32, VstInt32, VstIntPtr, void*, float);
typedef void     (*processRepl_t) (void*, sample_t**, sample_t**, VstInt32);

typedef struct {
  int32_t magic,_p04; void* dispatcher,*process,*setParameter,*getParameter;
  int32_t numPrograms,numParams,numInputs,numOutputs,flags,_p3c;
  void* resvd1,*resvd2; int32_t initialDelay,_p54,realQualities; float _p5c;
  void* object; int64_t _p68; int32_t uniqueID,version;
  processRepl_t processReplacing;        // +0x78
  void* processDoubleReplacing;          // +0x80
  char future[0x40];
} AEffect;

enum { effOpen=0, effClose=1, effSetProgram=2, effSetSampleRate=10,
       effSetBlockSize=11, effMainsChanged=13, effProcessEvents=25 };

typedef struct { double sampleRate,samplePos,nanoSeconds,ppqPos,tempo,barStartPos,
                 cycleStartPos,cycleEndPos; int32_t timeSigNumerator,timeSigDenominator,
                 smpteOffset,smpteFrameRate,flags,samplesToNextClock; } VstTimeInfo;
typedef struct { int32_t type,byteSize,deltaFrames,flags,noteLength;
                 int8_t noteOffset,detune,noteOffVel,res1; char midi[4]; int32_t _x[6]; } VstMidiEvent;
typedef struct { int32_t numEvents; void* reserved; void* events[8]; } VstEvents;

static VstTimeInfo GT = { 44100.0, 0,0,0,120.0,0,0,0, 4,4,0,0,0,0 };

static VstIntPtr hostCB(void* e,VstInt32 op,VstInt32 i,VstIntPtr v,void* p,float o){
    (void)e;(void)i;(void)v;(void)o;(void)p;
    switch(op){
      case 1:  return 2;                 // audioMasterVersion
      case 7:  return (VstIntPtr)&GT;    // audioMasterGetTime
      case 16: return 44100;             // audioMasterGetSampleRate
      case 17: return 256;               // audioMasterGetBlockSize
      case 22: return 2;                 // audioMasterGetCurrentProcessLevel
      default: return 0;
    }
}

static void ev_set(VstMidiEvent* m,int df,int st,int d1,int d2){
    memset(m,0,sizeof *m); m->type=1; m->byteSize=32; m->deltaFrames=df;
    m->midi[0]=(char)st; m->midi[1]=(char)d1; m->midi[2]=(char)d2;
}
static void block(AEffect* e, VstEvents* ev, VstInt32 n, sample_t* L, sample_t* R,
                  sample_t* zL, sample_t* zR){
    if(ev) ((dispatcher_t)e->dispatcher)(e, effProcessEvents,0,0,ev,0);
    sample_t* out[2]={L,R}; sample_t* in[2]={zL,zR};
    e->processReplacing(e, in, out, n);
}

static void wav_write(const char* p, sample_t* L, sample_t* R, size_t n, int sr){
    FILE* f=fopen(p,"wb"); if(!f){perror("wav");exit(1);}
    uint32_t nb=(uint32_t)(n*8); fwrite("RIFF",1,4,f); uint32_t v=36+nb; fwrite(&v,4,1,f);
    fwrite("WAVEfmt ",1,8,f); v=16; fwrite(&v,4,1,f); uint16_t h=3; fwrite(&h,2,1,f);
    h=2; fwrite(&h,2,1,f); fwrite(&sr,4,1,f); uint32_t bps=(uint32_t)(sr*8);
    fwrite(&bps,4,1,f); h=8; fwrite(&h,2,1,f); h=32; fwrite(&h,2,1,f);
    fwrite("data",1,4,f); fwrite(&nb,4,1,f);
    for(size_t i=0;i<n;i++){ fwrite(L+i,4,1,f); fwrite(R+i,4,1,f);} fclose(f);
}

int main(int argc, char** argv){
    setvbuf(stderr, NULL, _IONBF, 0);
    if(argc<3){ fprintf(stderr,"usage: %s <binary> <out.wav> [mains]\n",argv[0]); return 1; }
    int doMains = 0; for(int a=2;a<argc;++a) if(strstr(argv[a],"mains")) doMains=1;
    const int SR=44100, BLOCK=256, NOTES[3]={28,40,52};
    void* h=dlopen(argv[1], RTLD_NOW|RTLD_LOCAL);
    if(!h){ fprintf(stderr,"dlopen: %s\n", dlerror()); return 1; }
    typedef AEffect* (*entry_t)(audioMasterCB);
    entry_t en=(entry_t)dlsym(h,"VSTPluginMain"); if(!en) en=(entry_t)dlsym(h,"main_macho");
    if(!en){ fprintf(stderr,"no entry symbol\n"); return 1; }
    AEffect* e=en(hostCB);
    if(!e||e->magic!=0x56737450){ fprintf(stderr,"bad magic %x\n",e?(unsigned)e->magic:0); return 1; }

    dispatcher_t d=(dispatcher_t)e->dispatcher;
    fprintf(stderr,"open...\n");     d(e,effOpen,0,0,0,0);
    fprintf(stderr,"setSR...\n");    d(e,effSetSampleRate,0,0,0,(float)SR);
    fprintf(stderr,"setBlock...\n"); d(e,effSetBlockSize,0,BLOCK,0,0);
    if(doMains){ fprintf(stderr,"mainsOn...\n"); d(e,effMainsChanged,0,1,0,0); fprintf(stderr,"mainsOn done\n"); }
    // ---- RCA Stage 0.2: dump the original's seeded delay-line state ----
    for (int a=2;a<argc;++a) if (strstr(argv[a],"dumpvoices")) {
        // Scan all 16 programs: print gainL(+0x30), g(+0xd0), N(+0x100), pickup(+0x104) per program
        fprintf(stderr,"=== Per-program voice scan (note 40, velocity 100) ===\n");
        fprintf(stderr,"%4s %10s %10s %10s %10s %10s\n","prog","gainL","g","N","pickup","filt");
        for (int prog=0; prog<16; ++prog) {
            d(e,effSetProgram,0,prog,0,0);
            VstMidiEvent on; ev_set(&on,0,0x90,40,100); VstEvents eon={1,0,{&on}};
            d(e,effProcessEvents,0,0,&eon,0);
            { sample_t bl[64], z[64]; memset(z,0,sizeof z); sample_t*o[2]={bl,bl},*in[2]={z,z};
              e->processReplacing(e,in,o,4096); }  // 1 sample to trigger note-on gain computation
            char* vastring=(char*)e->object; if(!vastring) continue;
            char* voices=*(char**)(vastring+0x40f8); if(!voices) continue;
            char* v=*(char**)(voices+0x2b8); if(!v) continue;
            double gainL,g,filt; int N,pickup;
            __builtin_memcpy(&gainL,v+0x30,8); __builtin_memcpy(&g,v+0xd0,8);
            __builtin_memcpy(&filt,v+0xd8,8); __builtin_memcpy(&N,v+0x100,4);
            __builtin_memcpy(&pickup,v+0x104,4);
            fprintf(stderr,"%4d %10.6f %10.6f %10d %10d %10.6f\n",prog,gainL,g,N,pickup,filt);
        }
        fprintf(stderr,"===\n");
        // Full dump for program 0
        d(e,effSetProgram,0,0,0,0);
        VstMidiEvent on; ev_set(&on,0,0x90,40,100); VstEvents eon={1,0,{&on}};
        d(e,effProcessEvents,0,0,&eon,0);          // note-on
        { sample_t bl[4096], z[4096]; memset(z,0,sizeof z); sample_t*o[2]={bl,bl},*in[2]={z,z};
          e->processReplacing(e,in,o,4096); }      // 4096 samples -> fully evolved state
        char* vastring=(char*)e->object; if(!vastring){fprintf(stderr,"no obj\n");return 1;}
        char* voices=*(char**)(vastring+0x40f8);    if(!voices){fprintf(stderr,"no voices\n");return 1;}
        char* v=*(char**)(voices+0x2b8);
        int N; __builtin_memcpy(&N,v+0x100,4);
        float *dlA,*dlB; __builtin_memcpy(&dlA,v+0x110,8); __builtin_memcpy(&dlB,v+0x118,8);
        FILE* fr=fopen("/tmp/voice_raw.bin","wb"); fwrite(v,1,0x128,fr); fclose(fr);
        FILE* fa=fopen("/tmp/voice_dlA.bin","wb"); fwrite(dlA,4,N,fa); fclose(fa);
        FILE* fb=fopen("/tmp/voice_dlB.bin","wb"); fwrite(dlB,4,N,fb); fclose(fb);
        // ===== Shape investigation: seeded dumps (processReplacing=1) for {0,5,6,8,15} =====
        // Captures excitation table (4096 floats @ [voice+0x90]+0xf0), seeded dlA/dlB,
        // and full voice struct (0x140 bytes) immediately after note-on, before evolution.
        {
            fprintf(stderr,"=== Seeded dumps for ALL 16 programs ===\n");
            for (int prog=0; prog<16; ++prog) {
                d(e,effSetProgram,0,prog,0,0);
                VstMidiEvent son; ev_set(&son,0,0x90,40,100); VstEvents seon={1,0,{&son}};
                d(e,effProcessEvents,0,0,&seon,0);
                { sample_t sbl[64], sz[64]; memset(sz,0,sizeof sz);
                  sample_t* so[2]={sbl,sbl},* sin[2]={sz,sz};
                  e->processReplacing(e,sin,so,1); }   // 1 sample: capture table before evolution
                char* sobj=(char*)e->object; if(!sobj){fprintf(stderr,"prog %d: no obj\n",prog);continue;}
                char* svoc=*(char**)(sobj+0x40f8); if(!svoc){fprintf(stderr,"prog %d: no voices\n",prog);continue;}
                char* sv=*(char**)(svoc+0x2b8); if(!sv){fprintf(stderr,"prog %d: no voice\n",prog);continue;}
                char* sdata=*(char**)(sv+0x90); if(!sdata){fprintf(stderr,"prog %d: no data\n",prog);continue;}
                float* stable=(float*)(sdata+0xf0);
                char path[128];
                snprintf(path,sizeof path,"/tmp/table_prog%d.bin",prog);
                FILE* stf=fopen(path,"wb"); if(!stf){perror(path);continue;} fwrite(stable,4,4096,stf); fclose(stf);
                int tuniq=1; for(int i=1;i<4096;++i){ if(stable[i]!=stable[0]){tuniq=0;break;} }
                double tmin=1e30,tmax=-1e30;
                for(int i=0;i<4096;++i){double x=stable[i]; if(x<tmin)tmin=x; if(x>tmax)tmax=x;}
                fprintf(stderr,"prog %2d: table all_equal=%d first=%.4f min=%.4f max=%.4f\n",
                        prog,tuniq,stable[0],tmin,tmax);
            }
            fprintf(stderr,"===\n");
        }
        d(e,effClose,0,0,0,0); return 0;
    }

    const size_t sustain=SR/2, release=SR/4, step=sustain+release;
    const size_t total=16*3*step + SR;
    sample_t* gL=calloc(total,sizeof(sample_t)), *gR=calloc(total,sizeof(sample_t));
    sample_t blL[BLOCK], blR[BLOCK], zL[BLOCK], zR[BLOCK];
    memset(zL,0,sizeof zL); memset(zR,0,sizeof zR); size_t pos=0;

    for(int prog=0;prog<16;++prog){
        d(e,effSetProgram,0,prog,0,0);
        for(int ni=0;ni<3;++ni){
            VstMidiEvent on; ev_set(&on,0,0x90,NOTES[ni],100); VstEvents eon={1,0,{&on}};
            block(e,&eon,1,blL,blR,zL,zR);
            for(size_t p=0;p<sustain;p+=BLOCK){
                VstInt32 n=(VstInt32)((sustain-p<BLOCK)?sustain-p:BLOCK);
                block(e,0,n,blL,blR,zL,zR); memcpy(gL+pos,blL,n*sizeof *blL); memcpy(gR+pos,blR,n*sizeof *blR); pos+=n;
            }
            VstMidiEvent off; ev_set(&off,0,0x80,NOTES[ni],0); VstEvents eoff={1,0,{&off}};
            block(e,&eoff,1,blL,blR,zL,zR);
            for(size_t p=0;p<release;p+=BLOCK){
                VstInt32 n=(VstInt32)((release-p<BLOCK)?release-p:BLOCK);
                block(e,0,n,blL,blR,zL,zR); memcpy(gL+pos,blL,n*sizeof *blL); memcpy(gR+pos,blR,n*sizeof *blR); pos+=n;
            }
        }
    }
    for(size_t p=0;p<(size_t)SR;p+=BLOCK){
        VstInt32 n=(VstInt32)(((size_t)SR-p<BLOCK)?(size_t)SR-p:BLOCK);
        block(e,0,n,blL,blR,zL,zR); memcpy(gL+pos,blL,n*sizeof *blL); memcpy(gR+pos,blR,n*sizeof *blR); pos+=n;
    }
    wav_write(argv[2],gL,gR,total,SR);            // write before cleanup (plugin's mains-off path faults headlessly)
    double peak=0; for(size_t i=0;i<total;i++){ double a=gL[i]<0?-gL[i]:gL[i]; if(a>peak)peak=a; }
    fprintf(stderr,"wrote %s (%zu frames, %zus) peakL=%.4f\n",argv[2],total,total/SR,peak);
    // dump the original's runtime-generated excitation noise tables (raw@0x74a20, smooth@0x84a20)
    // to load verbatim in the reimpl -> sample-exact waveguide evolution. Compute ASLR slide
    // from the dispatcher pointer (binary vaddr 0x6258).
    { intptr_t slide = (intptr_t)(void*)e->dispatcher - 0x6258;
      const float *raw = (const float*)(0x74a20 + slide), *smo = (const float*)(0x84a20 + slide);
      FILE* f1 = fopen("/tmp/exc_raw.bin","wb"); FILE* f2 = fopen("/tmp/exc_smooth.bin","wb");
      if (f1 && f2) { fwrite(raw, 4, 16384, f1); fwrite(smo, 4, 16384, f2);
        fprintf(stderr,"dumped excitation tables (slide=%p, raw[0]=%.4f)\n",(void*)slide,(double)raw[0]); }
      if (f1) fclose(f1); if (f2) fclose(f2); }
    return 0;
}
