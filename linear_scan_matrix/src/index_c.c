/* C control: a 4-byte closed random index cycle.  Construction is before the
 * PMU interval; each timed iteration can learn its next address only by
 * loading next[index]. */
#define _POSIX_C_SOURCE 200809L
#include "pmu_scope.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static volatile uint32_t sink;
static uint64_t splitmix64(uint64_t*s){*s+=UINT64_C(0x9e3779b97f4a7c15);uint64_t x=*s;x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);}
static void shuffle(uint32_t*v,size_t n){uint64_t s=UINT64_C(20260909)^UINT64_C(0x12233445);for(size_t i=n;i>1;--i){size_t j=(size_t)(splitmix64(&s)%i);uint32_t t=v[i-1];v[i-1]=v[j];v[j]=t;}}
__attribute__((noinline)) static uint32_t chase(const uint32_t*next,uint32_t start,uint64_t operations){uint32_t index=start;for(uint64_t step=0;step<operations;++step)index=next[index];return index;}
int main(int argc,char**argv){if(argc!=3){fprintf(stderr,"usage: index_c bytes passes\n");return 64;}size_t bytes=(size_t)strtoull(argv[1],0,10);uint64_t passes=strtoull(argv[2],0,10);if(bytes<64||bytes%64||!passes){fprintf(stderr,"bytes must be a nonzero multiple of 64\n");return 64;}size_t entries=bytes/4;uint32_t*next=0,*order=malloc(entries*4);if(posix_memalign((void**)&next,64,bytes)||!order){perror("allocation");return 2;}for(size_t i=0;i<entries;++i)order[i]=(uint32_t)i;shuffle(order,entries);for(size_t i=0;i<entries;++i)next[order[i]]=order[(i+1)%entries];uint32_t start=order[0],expected=start;uint64_t operations=entries*passes;scan_pmu_scope scope;scan_pmu_result pmu;if(scan_pmu_open(&scope)||scan_pmu_start(&scope)){fprintf(stderr,"PMU setup failed: %s\n",strerror(errno));return 70;}uint64_t begun=scan_clock_now_ns(),checksum=chase(next,start,operations),elapsed=scan_clock_now_ns()-begun;if(scan_pmu_stop(&scope,&pmu)){fprintf(stderr,"PMU read failed\n");return 70;}int available=scan_pmu_is_available(&scope);scan_pmu_close(&scope);sink=checksum;if(checksum!=expected||!available||!pmu.time_enabled||pmu.time_enabled!=pmu.time_running){fprintf(stderr,"invalid checksum or PMU interval\n");return 65;}printf("bytes=%zu,passes=%"PRIu64",operations=%"PRIu64",elapsed_ns=%"PRIu64",checksum=%"PRIu64",expected_checksum=%"PRIu32",pmu_cycles=%"PRIu64",pmu_instructions=%"PRIu64",pmu_ref_cycles=%"PRIu64",pmu_time_enabled=%"PRIu64",pmu_time_running=%"PRIu64",pmu_available=%d\n",bytes,passes,operations,elapsed,checksum,expected,pmu.cycles,pmu.instructions,pmu.ref_cycles,pmu.time_enabled,pmu.time_running,available);free(next);free(order);}
