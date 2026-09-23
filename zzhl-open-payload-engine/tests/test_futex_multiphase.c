#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "dentry_fops_preflight.h"

struct state { struct oss_futex_phase phases[8]; size_t phase_count, verifies, advances; int narrowed, released, cleaned, ambiguous, exact_at, fail_deliver_at, always_changed; };
static int deliver(void *p, const struct oss_futex_phase *v) { struct state *s=p; size_t n=s->phase_count; s->phases[s->phase_count++]=*v; return s->fail_deliver_at<0 || (int)n!=s->fail_deliver_at; }
static int verify(void *p, const struct oss_fops_candidate *c, int *changed) { struct state *s=p; (void)c; size_t i=s->verifies++; if (s->ambiguous) { *changed=-2; return OSS_FOPS_PREFLIGHT_NO_MATCH; } *changed=(s->always_changed || i) ? 17 : -1; return (int)i==s->exact_at ? OSS_FOPS_PREFLIGHT_EXACT : OSS_FOPS_PREFLIGHT_NO_MATCH; }
static int advance(void *p) { ((struct state *)p)->advances++; return 1; }
static int narrow(void *p, size_t n) { ((struct state *)p)->narrowed=(int)n; return 1; }
static int release(void *p) { ((struct state *)p)->released=1; return 1; }
static int cleanup(void *p) { ((struct state *)p)->cleaned=1; return 1; }
static uint64_t to_page(void *p, uint64_t v) { (void)p; return v-0xffffff8000000000ULL+0x100000ULL; }

int main(void) {
  struct oss_fops_candidate c[OSS_FOPS_CANDIDATE_COUNT] = {0};
  for (size_t i=0; i<OSS_FOPS_CANDIDATE_COUNT; i++) c[i].page=0xffffff8001001000ULL+i*0x8000;
  struct state s = {.narrowed=-1,.exact_at=2,.fail_deliver_at=-1};
  struct oss_futex_multiphase_transport t = {.ctx=&s,.deliver=deliver,.verify=verify,.advance_pipe=advance,.narrow_pipe=narrow,.release_pipe=release,.cleanup_pipe=cleanup,.direct_to_page=to_page};
  struct oss_futex_multiphase_result r;
  assert(oss_run_futex_multiphase_preflight(c,0xffffff8002000000ULL,0xffffff8003000000ULL,&t,&r)==1);
  assert(r.scanned==3 && r.selected_candidate==2 && r.changed_pipe==17);
  assert(s.phase_count==6 && s.advances==2 && s.narrowed==17 && s.released==1);
  assert(s.cleaned==0);
  assert(s.phases[0].sequence==12 && s.phases[1].sequence==13);
  assert(s.phases[4].sequence==16 && s.phases[5].sequence==17);
  assert(s.phases[0].right==0xffffff8002000800ULL);
  assert(s.phases[2].right==0xffffff8002000828ULL && s.phases[1].right==0);
  assert(s.phases[0].parent==to_page(NULL,c[0].page));
  assert(s.phases[1].parent==to_page(NULL,c[0].page));
  assert(s.phases[0].lock==0xffffff8003000280ULL);
  assert(s.phases[1].lock==0xffffff8003000300ULL);
  assert(s.phases[4].lock==0xffffff8003000480ULL);
  assert(s.phases[5].lock==0xffffff8003000500ULL);

  memset(&s, 0, sizeof(s));
  s.narrowed=-1;
  s.ambiguous=1;
  s.exact_at=-1;
  s.fail_deliver_at=-1;
  assert(oss_run_futex_multiphase_preflight(c,0xffffff8002000000ULL,
             0xffffff8003000000ULL,&t,&r)==OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR);
  assert(s.phase_count==2 && s.verifies==1 && s.advances==0);
  assert(s.narrowed==-1 && s.released==0);

  memset(&s, 0, sizeof(s));
  s.narrowed=-1;
  s.exact_at=-1;
  s.fail_deliver_at=-1;
  s.always_changed=1;
  assert(oss_run_futex_multiphase_preflight(c,0xffffff8002000000ULL,
             0xffffff8003000000ULL,&t,&r)==OSS_FOPS_PREFLIGHT_NO_MATCH);
  assert(r.scanned==4 && r.selected_candidate==(size_t)-1 && r.changed_pipe==17);
  assert(s.phase_count==8 && s.verifies==4 && s.advances==3);
  assert(s.narrowed==17 && s.released==0 && s.cleaned==1);
  for (size_t i=0; i<OSS_FOPS_CANDIDATE_COUNT; i++) {
    assert(s.phases[i*2].sequence==12+i*2);
    assert(s.phases[i*2+1].sequence==13+i*2);
    assert(s.phases[i*2].lock==0xffffff8003000280ULL+i*0x100);
    assert(s.phases[i*2+1].lock==0xffffff8003000300ULL+i*0x100);
  }

  memset(&s, 0, sizeof(s));
  s.narrowed=-1;
  s.exact_at=-1;
  s.fail_deliver_at=1;
  assert(oss_run_futex_multiphase_preflight(c,0xffffff8002000000ULL,
             0xffffff8003000000ULL,&t,&r)==OSS_FOPS_PREFLIGHT_RESTORE_ERROR);
  assert(s.phase_count==2 && s.verifies==1);
  assert(s.narrowed==-1 && s.released==0);

  struct oss_futex_multiphase_transport missing_cleanup=t;
  missing_cleanup.cleanup_pipe=NULL;
  assert(oss_run_futex_multiphase_preflight(c,0xffffff8002000000ULL,
             0xffffff8003000000ULL,&missing_cleanup,&r)==
         OSS_FOPS_PREFLIGHT_TRANSPORT_ERROR);
  return 0;
}
