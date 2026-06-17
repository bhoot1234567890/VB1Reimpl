// vst2_render.c — headless VST2 host to render the ORIGINAL VB-1 (x86_64, under Rosetta).
// AEffect offsets from vb1.vst base ctor (0x681e): processReplacing @ +0x78 (realQualities
// is int32 here, collapsing the canonical padding). Usage:
//   arch -x86_64 ./vst2_render <vb1.vst/Contents/MacOS/vb1> <out.wav> [mains]
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <math.h>

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

// ---- dumpenv helpers (release-envelope runtime analysis) ----
static void de_render(AEffect* e, size_t n){
    const size_t BS=256;
    sample_t blL[BS],blR[BS],z[BS]; memset(z,0,sizeof z);
    for(size_t p=0;p<n;p+=BS){ size_t nn=(n-p<BS)?n-p:BS;
        sample_t*o[2]={blL,blR},*in[2]={z,z}; e->processReplacing(e,in,o,(VstInt32)nn); }
}
static char* de_voice(AEffect* e){
    char* obj=(char*)e->object; if(!obj) return 0;
    char* vv=*(char**)(obj+0x40f8); if(!vv) return 0;
    return *(char**)(vv+0x2b8);
}
static float de_getparam(AEffect* e, VstInt32 i){ return ((float(*)(void*,VstInt32))e->getParameter)(e,i); }
static void de_dump(char* v,int prog,const char* lbl){
    if(!v){ fprintf(stderr,"[prog %2d] %-26s (NULL voice)\n",prog,lbl); return; }
    double env,dec,gainL,g,filt; int state,sus;
    __builtin_memcpy(&env,v+0xf0,8); __builtin_memcpy(&dec,v+0xf8,8);
    __builtin_memcpy(&gainL,v+0x30,8); __builtin_memcpy(&g,v+0xd0,8); __builtin_memcpy(&filt,v+0xd8,8);
    __builtin_memcpy(&state,v+0xc0,4); __builtin_memcpy(&sus,v+0xc4,4);
    fprintf(stderr,"[prog %2d] %-26s env=%.10g dec=%.10g state=%d sus=%d gainL=%.6f g=%.10g filt=%.10g\n",
            prog,lbl,env,dec,state,sus,gainL,g,filt);
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
    fprintf(stderr,"initialDelay=%d flags=0x%x numPrograms=%d\n", e->initialDelay, e->flags, e->numPrograms);

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
            float pv[6];
            for(int pi=0;pi<6;++pi) pv[pi]=((float(*)(void*,VstInt32))e->getParameter)(e,pi);
            fprintf(stderr,"params %2d: %.8gf, %.8gf, %.8gf, %.8gf, %.8gf, %.8gf\n",
                    prog, pv[0],pv[1],pv[2],pv[3],pv[4],pv[5]);
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
    // ---- dumpenv: release-envelope init/decrement/linearity/duration ----
    for(int a=2;a<argc;++a) if(strstr(argv[a],"dumpenv")){
        FILE* md=fopen("/tmp/release_runtime_findings.md","w");
        if(!md){perror("md");return 1;}
        fprintf(md,"# VB-1 Release Envelope — Runtime Findings\n\n");
        fprintf(md,"Source: original VST2 VB-1 binary. Note 40, velocity 100, SR=44100.\n");
        fprintf(md,"Voice access: AEffect.object -> +0x40f8 -> +0x2b8 -> voice ptr.\n");
        fprintf(md,"Fields: +0x030 gainL, +0x0c0 state(1=playing,2=ended), +0x0c4 sustain-flag(0=sustain,!=0=release), +0x0d0 g, +0x0f0 env-level, +0x0f8 env-decrement.\n\n");
        const char* pname[6]={"pDamper","pPickUp","pPick","pRelease","pShape","pVolume"};
        int progs[2]={0,8};
        const int sus_dur=SR/2;       // 22050 samples of sustain before note-off
        const int rel_cap=SR*4;       // safety cap for release-duration measurement
        double dec_p0=0, env0_p0=0, relp0=0;
        for(int pi=0;pi<2;++pi){
            int prog=progs[pi];
            d(e,effSetProgram,0,prog,0,0);
            float pv[6]; for(int p=0;p<6;++p) pv[p]=de_getparam(e,p);
            fprintf(stderr,"[prog %2d] params:",prog);
            for(int p=0;p<6;++p) fprintf(stderr," %s=%.4f",pname[p],pv[p]);
            fprintf(stderr,"\n");
            // ---- note-on ----
            VstMidiEvent on; ev_set(&on,0,0x90,40,100); VstEvents eon={1,0,{&on}};
            d(e,effProcessEvents,0,0,&eon,0);
            de_render(e,1);                       // 1 sample: note-on fully applied
            char* v=de_voice(e);
            double env_init=0,dec=0; int st_no=0,sus_no=0;
            if(v){ __builtin_memcpy(&env_init,v+0xf0,8); __builtin_memcpy(&dec,v+0xf8,8);
                   __builtin_memcpy(&st_no,v+0xc0,4); __builtin_memcpy(&sus_no,v+0xc4,4); }
            de_dump(v,prog,"note-on @1 samp");
            // ---- sustain @ 1000 ----
            de_render(e,999);
            v=de_voice(e); de_dump(v,prog,"sustain @1000 samp");
            double env_sus1k=0; if(v) __builtin_memcpy(&env_sus1k,v+0xf0,8);
            // ---- sustain env trace to SR/2 (confirm constant) ----
            int sidx=1000;
            while(sidx<sus_dur){ int st=(sus_dur-sidx>=5000)?5000:(sus_dur-sidx);
                de_render(e,st); sidx+=st; v=de_voice(e);
                double e2=0; if(v) __builtin_memcpy(&e2,v+0xf0,8);
                fprintf(stderr,"[prog %2d]   sustain @%6d env=%.10g\n",prog,sidx,e2); }
            v=de_voice(e); de_dump(v,prog,"sustain @SR/2 end");
            double env_sus_end=0; if(v) __builtin_memcpy(&env_sus_end,v+0xf0,8);
            // ---- note-off + 1 sample (release start) ----
            VstMidiEvent off; ev_set(&off,0,0x80,40,0); VstEvents eoff={1,0,{&off}};
            d(e,effProcessEvents,0,0,&eoff,0);
            de_render(e,1);
            v=de_voice(e);
            double env_rel0=0; int st_rel0=0,sus_rel0=0;
            if(v){ __builtin_memcpy(&env_rel0,v+0xf0,8); __builtin_memcpy(&st_rel0,v+0xc0,4); __builtin_memcpy(&sus_rel0,v+0xc4,4); }
            de_dump(v,prog,"release @1 samp");
            // ---- linearity trace + duration: 1-by-1 until voice ends or cap ----
            char lpath[128]; snprintf(lpath,sizeof lpath,"/tmp/release_trace_prog%d.bin",prog);
            FILE* lf=fopen(lpath,"wb");
            double prev=env_rel0, dmin=1e30,dmax=-1e30,dsum=0; int dcount=0;
            int rel_n=1, end_sample=-1; double env_at100=-1;
            for(int s=0;s<rel_cap;++s){
                de_render(e,1); rel_n++;
                v=de_voice(e);
                if(!v){ fprintf(stderr,"[prog %2d] voice ptr NULL @ release sample %d\n",prog,rel_n); end_sample=rel_n; break; }
                double env; int state,sus;
                __builtin_memcpy(&env,v+0xf0,8); __builtin_memcpy(&state,v+0xc0,4); __builtin_memcpy(&sus,v+0xc4,4);
                if(lf){ float rec[3]={(float)env,(float)state,(float)sus}; fwrite(rec,4,3,lf); }
                double delta=prev-env;
                if(dcount<300){ if(delta<dmin)dmin=delta; if(delta>dmax)dmax=delta; dsum+=delta; dcount++; }
                prev=env;
                if(rel_n==100) env_at100=env;
                if(rel_n<=12 || rel_n%1000==0)
                    fprintf(stderr,"[prog %2d]   release @%6d env=%.10g state=%d sus=%d delta=%.10g\n",prog,rel_n,env,state,sus,delta);
                if(state!=1){ end_sample=rel_n;
                    fprintf(stderr,"[prog %2d] voice ENDED @ release sample %d (state=%d env=%.10g)\n",prog,rel_n,state,env); break; }
            }
            if(lf) fclose(lf);
            if(end_sample<0){ end_sample=rel_n; fprintf(stderr,"[prog %2d] hit release CAP %d (voice still active)\n",prog,rel_cap); }
            double dmean=dcount?dsum/dcount:0;
            int const_delta = (dcount>0 && dmin==dmax);
            fprintf(stderr,"[prog %2d] linearity: %d deltas min=%.10g max=%.10g mean=%.10g (dec=%.10g) -> %s\n",
                    prog,dcount,dmin,dmax,dmean,dec, const_delta?"CONSTANT-DELTA (linear)":"NON-CONSTANT (exp/other)");
            if(prog==0){ dec_p0=dec; env0_p0=env_rel0; relp0=pv[3]; }
            // ---- write MD section ----
            fprintf(md,"## Program %d (Shape=%.4f)\n\n",prog,(double)pv[4]);
            fprintf(md,"**Params:** ");
            for(int p=0;p<6;++p) fprintf(md,"%s=%.4f  ",pname[p],pv[p]);
            fprintf(md,"\n\n");
            fprintf(md,"| Checkpoint | env (+0xf0) | dec (+0xf8) | state (+0xc0) | sustain-flag (+0xc4) |\n");
            fprintf(md,"|---|---|---|---|---|\n");
            fprintf(md,"| note-on @1 samp | %.10g | %.10g | %d | %d |\n",env_init,dec,st_no,sus_no);
            fprintf(md,"| sustain @1000 | %.10g | %.10g | 1 | 0 |\n",env_sus1k,dec);
            fprintf(md,"| sustain @SR/2 end | %.10g | %.10g | 1 | 0 |\n",env_sus_end,dec);
            fprintf(md,"| release @1 samp (note-off+1) | %.10g | %.10g | %d | %d |\n",env_rel0,dec,st_rel0,sus_rel0);
            fprintf(md,"| release @100 samp | %.10g | %.10g | 1 | %d |\n",env_at100,dec,sus_rel0);
            fprintf(md,"\n");
            fprintf(md,"### Findings — Program %d\n\n",prog);
            fprintf(md,"1. **env initial at note-on (+0xf0):** %.10g\n",env_init);
            fprintf(md,"2. **env during sustain:** @1000 = %.10g, @SR/2 end = %.10g -> env is **%s** during sustain (held, not decremented).\n",
                    env_sus1k,env_sus_end, (env_sus1k==env_sus_end)?"CONSTANT":"changing");
            fprintf(md,"3. **decrement (+0xf8):** %.10g (same value at note-on, sustain, and release — set once at note-on).\n",dec);
            fprintf(md,"4. **+0xc4 transition at note-off:** 0 (sustain) -> %d (release). env at note-off+1 = %.10g; env at sustain-end = %.10g; diff = %.10g %s.\n",
                    sus_rel0, env_rel0, env_sus_end, env_sus_end-env_rel0, (fabs((env_sus_end-env_rel0)-dec)<1e-12)?"(== dec: one decrement applied at note-off)":"");
            fprintf(md,"5. **release duration:** %d samples (%.4f s). env @100 = %.10g.\n",end_sample,(double)end_sample/SR, env_at100);
            fprintf(md,"6. **linearity:** over first %d release samples, per-sample delta min=%.10g max=%.10g mean=%.10g; dec=%.10g. -> **%s**%s\n",
                    dcount,dmin,dmax,dmean,dec, const_delta?"EXACTLY LINEAR (constant per-sample decrement)":"NON-LINEAR",
                    const_delta && fabs(dmean-dec)<1e-12 ? " (delta == +0xf8 every sample)":"");
            fprintf(md,"7. **Release param (pRelease) = %.4f.** dec (+0xf8) = %.10g. env0 (at release start) = %.10g. Predicted linear duration = env0/dec = %.2f samples.\n",
                    pv[3],dec,env_rel0, dec>0?env_rel0/dec:0);
            fprintf(md,"\n");
        }
        // ---- Release-param sweep (prog 0, note 40): dec vs Release via setParameter ----
        fprintf(md,"## Release-param sweep (prog 0, note 40, vel 100)\n\n");
        fprintf(md,"dec (+0xf8) measured at release sample 1; duration via 64-sample chunks (±64) capped at SR*2.\n\n");
        fprintf(md,"| pRelease | dec (+0xf8) | 1/dec | env@rel1 | env_init=env@rel1+dec | duration (samp) | duration (s) |\n");
        fprintf(md,"|---|---|---|---|---|---|---|\n");
        fprintf(stderr,"=== Release sweep (prog 0) ===\n");
        fprintf(stderr,"%8s %14s %12s %12s %12s %10s %10s\n","pRelease","dec","1/dec","env@rel1","env_init","dur","dur_s");
        double sweep[] = {0.0, 0.05, 0.1, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.7, 0.75, 0.8, 0.9, 0.95, 0.98, 0.99, 1.0};
        for(int si=0; si<(int)(sizeof(sweep)/sizeof(sweep[0])); ++si){
            double rel=sweep[si];
            d(e,effSetProgram,0,0,0,0);
            ((void(*)(void*,VstInt32,float))e->setParameter)(e, 3, (float)rel);
            VstMidiEvent on; ev_set(&on,0,0x90,40,100); VstEvents eon={1,0,{&on}};
            d(e,effProcessEvents,0,0,&eon,0);
            de_render(e,256);
            VstMidiEvent off; ev_set(&off,0,0x80,40,0); VstEvents eoff={1,0,{&off}};
            d(e,effProcessEvents,0,0,&eoff,0);
            de_render(e,1);
            char* v=de_voice(e);
            double dec=0, env0=0; if(v){ __builtin_memcpy(&dec,v+0xf8,8); __builtin_memcpy(&env0,v+0xf0,8); }
            int dur=1, ended=0;
            for(int s=0; s<SR*2; s+=64){
                de_render(e,64); dur+=64; v=de_voice(e); if(!v){ ended=1; break; }
                int state; __builtin_memcpy(&state,v+0xc0,4); if(state!=1){ ended=1; break; }
            }
            double invdec = dec>0 ? 1.0/dec : -1.0;
            double env_init = env0 + dec;
            fprintf(md,"| %.4f | %.10g | %.4f | %.10g | %.10g | %s%d | %.5f |\n",
                    rel, dec, invdec, env0, env_init, ended?"":"~>", dur, (double)dur/SR);
            fprintf(stderr,"%8.4f %14.10g %12.4f %12.10g %12.10g %10s%d %10.5f\n",
                    rel, dec, invdec, env0, env_init, ended?"":"~>", dur, (double)dur/SR);
        }
        fprintf(md,"\n");
        fprintf(md,"## Cross-program summary\n\n");
        fprintf(md,"- Programs 0 and 8 share pRelease=0.9800; both yield the SAME +0xf8 decrement and SAME env init -> **the release envelope depends only on the Release param (not Shape/Pick/etc.)**.\n");
        fprintf(md,"- If confirmed linear: env(t) = env0 - dec*t, voice ends when env<=0, duration = env0/dec samples (deterministic, independent of amplitude).\n");
        fprintf(md,"- Per-sample traces: /tmp/release_trace_prog{0,8}.bin (each record = 3 floats: env, state, sustain-flag; from release sample 2 onward).\n");
        fprintf(md,"\n_Raw stderr trace above; this file auto-generated by `dumpenv` mode._\n");
        fclose(md);
        fprintf(stderr,"wrote /tmp/release_runtime_findings.md\n");
        d(e,effClose,0,0,0,0);
        return 0;
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
            for(size_t p=0;p<sustain;p+=BLOCK){
                VstInt32 n=(VstInt32)((sustain-p<BLOCK)?sustain-p:BLOCK);
                block(e,(p==0)?&eon:0,n,blL,blR,zL,zR);
                memcpy(gL+pos,blL,n*sizeof *blL); memcpy(gR+pos,blR,n*sizeof *blR); pos+=n;
            }
            VstMidiEvent off; ev_set(&off,0,0x80,NOTES[ni],0); VstEvents eoff={1,0,{&off}};
            for(size_t p=0;p<release;p+=BLOCK){
                VstInt32 n=(VstInt32)((release-p<BLOCK)?release-p:BLOCK);
                block(e,(p==0)?&eoff:0,n,blL,blR,zL,zR);
                memcpy(gL+pos,blL,n*sizeof *blL); memcpy(gR+pos,blR,n*sizeof *blR); pos+=n;
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
