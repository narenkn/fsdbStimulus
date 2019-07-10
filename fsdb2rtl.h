#ifndef FSDB2RTL_H
#define FSDB2RTL_H

#include <cassert>
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <iomanip>
#include <sstream>
#include <cmath>
#include "acc_user.h"
#include "vpi_user.h"
#include "fsdbShr.h"
#include "ffrAPI.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>

#ifdef __cplusplus
#define EXTERN extern "C"
#endif

using namespace std;

#define VPI_CB_TIMESCALE -12 // picoseconds (10 ** -12 seconds)

#define BitsInAval (sizeof(PLI_INT32)<<3)

#define VPI_CANONICAL_SIZE(S) (((S)+31)>>5)

struct svType {
  uint32_t uWordSize /* # bits */;
  uint32_t uArraySize /* #svBitVec32 required for uArraySize */;
  fsdbVarIdcode idcode;
  ffrVCTrvsHdl  var_hdl;
  virtual void update(byte_T *vcptr) = 0;
  virtual void size(uint32_t S=1) = 0;
  string rtlName;
  vpiHandle vpih;
  void Attach(void) {
    vpih = vpi_handle_by_name((PLI_BYTE8 *)(rtlName.c_str()), NULL);
    assert(NULL != vpih);
  }

svType(uint32_t S, char *n):
  uWordSize(S), uArraySize(VPI_CANONICAL_SIZE(S)),
    rtlName(n) {
    idcode = -1;
    var_hdl = NULL;
    vpih = NULL;
  }

  ~svType() {
    if (var_hdl)
      var_hdl->ffrFree();
    if (vpih)
      vpi_free_object(vpih);
  }

};

struct svBitT : public svType {
  p_vpi_vecval Value;

  void update(byte_T *vcptr);

  svBitT(uint32_t S, char *n);
  ~svBitT();

  virtual void size(uint32_t S=1);
};

struct svLogicT : public svType {
  p_vpi_vecval Value;

  void update(byte_T *vcptr);

  svLogicT(uint32_t S, char *n);
  ~svLogicT();

  virtual void size(uint32_t S=1);
};

#endif
