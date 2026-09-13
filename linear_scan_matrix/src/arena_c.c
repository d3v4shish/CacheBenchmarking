/* C control for the arena pointer-chase experiment.
 *
 * A Node is precisely one, 64-byte aligned cache line.  `next` chooses the
 * address of the next node, so the following load cannot issue until this
 * load has returned.  Building the permutation and filling payloads happens
 * before the PMU interval; the timed loop does only the dependency chain.
 */
#define _POSIX_C_SOURCE 200809L
#include "pmu_scope.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct __attribute__((aligned(64))) { uint32_t next, value; uint8_t padding[56]; } Node;
_Static_assert(sizeof(Node) == 64, "Node must occupy one cache line");
static volatile uint64_t sink;
static uint64_t splitmix64(uint64_t *s) { *s += UINT64_C(0x9e3779b97f4a7c15); uint64_t x=*s; x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9); x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb); return x^(x>>31); }
static void shuffle(uint32_t *order, size_t n) { uint64_t s=UINT64_C(20260909)^UINT64_C(0xabcddcba); for(size_t i=n;i>1;--i){ size_t j=(size_t)(splitmix64(&s)%i); uint32_t t=order[i-1]; order[i-1]=order[j]; order[j]=t; } }
__attribute__((noinline)) static uint64_t chase(const Node *nodes, uint32_t start, uint64_t operations) {
  uint32_t index=start; uint64_t sum=0;
  for(uint64_t step=0;step<operations;++step) { const Node *node=&nodes[index]; sum += node->value; index=node->next; }
  return sum ^ index;
}
int main(int argc,char **argv) {
  if(argc!=3){fprintf(stderr,"usage: %s footprint_bytes passes\n",argv[0]);return 64;}
  size_t bytes=(size_t)strtoull(argv[1],0,10); uint64_t passes=strtoull(argv[2],0,10);
  if(bytes<1024||bytes%64||!passes){fprintf(stderr,"footprint must be a nonzero multiple of 64 bytes\n");return 64;}
  size_t n=bytes/64; Node *nodes=0; uint32_t *order=malloc(n*sizeof(*order));
  if(posix_memalign((void**)&nodes,64,bytes)||!order){perror("allocation");free(nodes);free(order);return 2;}
  for (size_t i = 0; i < n; ++i) order[i] = (uint32_t)i;
  shuffle(order, n);
  uint64_t state=UINT64_C(20260909),one=0;
  for(size_t i=0;i<n;++i){nodes[order[i]].next=order[(i+1)%n];nodes[order[i]].value=(uint32_t)splitmix64(&state);one+=nodes[order[i]].value;}
  uint32_t start=order[0]; uint64_t operations=(uint64_t)n*passes, expected=one*passes^start;
  scan_pmu_scope scope; scan_pmu_result pmu; if(scan_pmu_open(&scope)||scan_pmu_start(&scope)){fprintf(stderr,"PMU setup failed: %s\n",strerror(errno));return 70;}
  uint64_t begun=scan_clock_now_ns(),checksum=chase(nodes,start,operations),elapsed=scan_clock_now_ns()-begun;
  if(scan_pmu_stop(&scope,&pmu)){fprintf(stderr,"PMU read failed\n");scan_pmu_close(&scope);return 70;} int available=scan_pmu_is_available(&scope);scan_pmu_close(&scope);sink=checksum;
  if(checksum!=expected||!available||!pmu.time_enabled||pmu.time_enabled!=pmu.time_running){fprintf(stderr,"invalid checksum or multiplexed PMU interval\n");return 65;}
  printf("bytes=%zu,passes=%"PRIu64",operations=%"PRIu64",elapsed_ns=%"PRIu64",checksum=%"PRIu64",expected_checksum=%"PRIu64",pmu_cycles=%"PRIu64",pmu_instructions=%"PRIu64",pmu_ref_cycles=%"PRIu64",pmu_time_enabled=%"PRIu64",pmu_time_running=%"PRIu64",pmu_available=%d\n",bytes,passes,operations,elapsed,checksum,expected,pmu.cycles,pmu.instructions,pmu.ref_cycles,pmu.time_enabled,pmu.time_running,available);free(nodes);free(order);
}
